// SPDX-License-Identifier: GPL-2.0-only
/* Tell the Bluetooth controller which of its channels Wi-Fi uses (AFH host channel
 * classification), or read the channel map a link hops over.
 *
 *   y2-bt-afh <wifi-centre-MHz>   mask the channels of that Wi-Fi channel
 *   y2-bt-afh clear               every channel may be used again
 *   y2-bt-afh read                the first connection's channel map
 *
 * Wi-Fi and Bluetooth share the Y2's one 2.4 GHz antenna. The chip takes turns between its own
 * two radios, but the access point transmits whenever it likes; Bluetooth hopping across the
 * access point's channel loses those packets and retries. Measured on a player next to its
 * router (2427 MHz, 2026-11-10): 66-70 of 79 channels in use, only 4 of the router's ~22 left
 * out. Phones give the controller the Wi-Fi channel; nothing did here. One HCI command on a raw
 * socket, as hcitool sends it; BlueZ has no interface for it. Prints one JSON line. */
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* A Wi-Fi channel is 20 MHz wide, 22 with its skirts (802.11b): 11 MHz to each side. */
#define Y2_AFH_HALF_WIDTH_MHZ 11
#define Y2_AFH_CHANNELS 79

/* The classification for a Wi-Fi channel centred on `mhz` (0: none): bit n is Bluetooth channel
 * n at 2402 + n MHz, set while the channel may be used. Returns how many are masked. */
static unsigned y2_afh_map(unsigned mhz, uint8_t map[10])
{
	unsigned masked = 0;
	memset(map, 0, 10);
	for (unsigned n = 0; n < Y2_AFH_CHANNELS; n++) {
		int distance = (int)(2402 + n) - (int)mhz;
		if (mhz && distance >= -Y2_AFH_HALF_WIDTH_MHZ && distance <= Y2_AFH_HALF_WIDTH_MHZ)
			masked++;
		else
			map[n / 8] |= (uint8_t)(1u << (n % 8));
	}
	return masked;
}

/* A centre frequency of the 2.4 GHz band (channels 1 to 14), or 0. */
static unsigned y2_afh_wifi_mhz(const char *text)
{
	char *end = NULL;
	errno = 0;
	unsigned long mhz = strtoul(text, &end, 10);
	if (errno || !end || end == text || *end || mhz < 2412 || mhz > 2484)
		return 0;
	return (unsigned)mhz;
}

#ifndef Y2_BT_AFH_TEST
#define BTPROTO_HCI 1
#define SOL_HCI 0
#define HCI_FILTER 2
#define HCIGETCONNLIST _IOR('H', 212, int)
struct sockaddr_hci { sa_family_t family; unsigned short dev; unsigned short channel; };
struct hci_filter { uint32_t type_mask; uint32_t event_mask[2]; uint16_t opcode; };
struct hci_conn_info { uint16_t handle; uint8_t bdaddr[6]; uint8_t type, out; uint16_t state; uint32_t link_mode; };
struct hci_conn_list_req { uint16_t dev_id, conn_num; struct hci_conn_info info[4]; };

/* Send one command and wait for its Command Complete; the return parameters' length, or -1. */
static int command(int fd, uint16_t opcode, const uint8_t *parameters, unsigned length, uint8_t *reply, unsigned max)
{
	uint8_t packet[64] = {1, (uint8_t)(opcode & 0xff), (uint8_t)(opcode >> 8), (uint8_t)length};
	if (length > sizeof(packet) - 4)
		return -1;
	memcpy(packet + 4, parameters, length);
	if (write(fd, packet, 4 + length) != (ssize_t)(4 + length))
		return -1;
	for (unsigned tries = 0; tries < 32; tries++) {
		struct pollfd wait = {fd, POLLIN, 0};
		uint8_t event[260];
		if (poll(&wait, 1, 2000) <= 0)
			return -1;
		ssize_t size = read(fd, event, sizeof(event));
		if (size >= 6 && event[0] == 4 && event[1] == 0x0e && (event[4] | event[5] << 8) == opcode) {
			unsigned count = (unsigned)size - 6 < max ? (unsigned)size - 6 : max;
			memcpy(reply, event + 6, count);
			return (int)count;
		}
	}
	return -1;
}

static int fail(const char *reason)
{
	printf("{\"ok\":false,\"reason\":\"%s\"}\n", reason);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return fail("usage");
	int reading = !strcmp(argv[1], "read"), clearing = !strcmp(argv[1], "clear");
	unsigned mhz = reading || clearing ? 0 : y2_afh_wifi_mhz(argv[1]);
	if (!reading && !clearing && !mhz)
		return fail("not_a_2g4_wifi_frequency");
	int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
	struct sockaddr_hci address = {AF_BLUETOOTH, 0, 0};
	struct hci_filter filter = {1u << 4, {1u << 0x0e, 0}, 0};
	if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    setsockopt(fd, SOL_HCI, HCI_FILTER, &filter, sizeof(filter)) < 0)
		return fail("no_adapter");
	uint8_t reply[32];
	if (reading) {
		struct hci_conn_list_req links = {0, 4, {{0}}};
		if (ioctl(fd, HCIGETCONNLIST, &links) < 0 || !links.conn_num)
			return fail("no_connection");
		uint8_t handle[2] = {(uint8_t)(links.info[0].handle & 0xff), (uint8_t)(links.info[0].handle >> 8)};
		/* Read AFH Channel Map: status, handle, mode, map. */
		if (command(fd, 0x1406, handle, 2, reply, sizeof(reply)) < 14 || reply[0])
			return fail("read_refused");
		unsigned used = 0;
		printf("{\"ok\":true,\"map\":\"");
		for (unsigned n = 0; n < Y2_AFH_CHANNELS; n++) {
			unsigned on = reply[4 + n / 8] >> (n % 8) & 1;
			used += on;
			putchar(on ? '1' : '.');
		}
		printf("\",\"used\":%u}\n", used);
		return 0;
	}
	uint8_t map[10];
	unsigned masked = y2_afh_map(mhz, map);
	/* Set AFH Host Channel Classification: status. */
	if (command(fd, 0x0c3f, map, sizeof(map), reply, sizeof(reply)) < 1)
		return fail("no_answer");
	if (reply[0]) {
		printf("{\"ok\":false,\"reason\":\"refused\",\"status\":%u}\n", reply[0]);
		return 1;
	}
	printf("{\"ok\":true,\"wifi_mhz\":%u,\"masked\":%u}\n", mhz, masked);
	return 0;
}
#endif
