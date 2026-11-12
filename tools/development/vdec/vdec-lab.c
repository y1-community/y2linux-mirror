// SPDX-License-Identifier: GPL-2.0-only
/* Bring-up bench: run the driver's own register code (kernel/platform/vdec-h264.c and vdec-mpeg4.c,
 * compiled in) from userspace through the driver's debugfs register file, so that the sequence can be
 * changed and tried without a new kernel. The kernel still owns power, memory and the interrupt.
 *
 *   vdec-lab jobs.bin frames.raw [-v]     -v prints every register access
 *
 * Runs on the player as root. Not part of the product. */
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "../../../kernel/platform/vdec.h"

#define DEBUG "/sys/kernel/debug/y2_vdec/"
static int reg_fd, verbose;

double lab_now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
u32 lab_readl(uintptr_t address)
{
	char text[40];
	unsigned at, value = 0;
	int n = snprintf(text, sizeof(text), "%x", (unsigned)address);
	if (pwrite(reg_fd, text, n, 0) != n) perror("reg select");
	n = pread(reg_fd, text, sizeof(text) - 1, 0);
	if (n <= 0) { perror("reg read"); exit(1); }
	text[n] = 0;
	sscanf(text, "%x %x", &at, &value);
	if (verbose) printf("R %05x = %08x\n", (unsigned)address, value);
	return value;
}
void lab_writel(u32 value, uintptr_t address)
{
	char text[40];
	int n = snprintf(text, sizeof(text), "%x %x", (unsigned)address, value);
	if (verbose) printf("W %05x = %08x\n", (unsigned)address, value);
	if (pwrite(reg_fd, text, n, 0) != n) { perror("reg write"); exit(1); }
}

static void put(const char *file, const char *text)
{
	int fd = open(file, O_WRONLY);
	if (fd < 0 || write(fd, text, strlen(text)) < 0) { perror(file); exit(1); }
	close(fd);
}

/* Addresses of the buffers the kernel allocated for this configuration. */
static void read_buffers(struct y2_vdec *v)
{
	FILE *f = fopen(DEBUG "buffers", "r");
	char line[160];
	unsigned n, a, b, size;
	if (!f) { perror("buffers"); exit(1); }
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "bits %x %u", &a, &size) == 2) { v->bits.dma = a; v->bits.size = size; }
		else if (sscanf(line, "work_a %x", &a) == 1) v->work_a.dma = a;
		else if (sscanf(line, "work_b %x", &a) == 1) v->work_b.dma = a;
		else if (sscanf(line, "work %x", &a) == 1) v->work.dma = a;
		else if (sscanf(line, "frame %u %x mv %x %u", &n, &a, &b, &size) == 4) {
			v->frame[n].dma = a;
			v->mv[n].dma = b;
			v->mv[n].size = size;
		}
	}
	fclose(f);
}

/* The bench has no call that writes cached bytes back to memory or drops stale ones (the ARM
 * cacheflush call only reaches the unified cache level). Sweeping a buffer much larger than the caches
 * pushes the bitstream out to memory and forgets old frame lines; the driver's own path syncs properly. */
static uint8_t *sweep_buffer;
static void sweep_caches(unsigned seed)
{
	for (unsigned pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < 16u << 20; i += 32) sweep_buffer[i] = seed + pass + i;
}

static void report(struct y2_vdec *v, unsigned n, int ret, u32 error, u32 complete, u32 x, u32 y,
		   double waited)
{
	printf("picture %u start=%d trace=%x %x %x %x %x %x %x %x error=%#x complete=%u mb=%u,%u %.1f ms\n",
	       n, ret, v->trace[0], v->trace[1], v->trace[2], v->trace[3], v->trace[4], v->trace[5],
	       v->trace[6], v->trace[7], error, complete, x, y, waited / 1e3);
}

static unsigned run_h264(struct y2_vdec *v, FILE *jobs, uint8_t *bitstream, unsigned n)
{
	static struct y2_vdec_h264 picture;
	double start, waited = 0;
	int ret;
	if (fread(&picture, sizeof(picture), 1, jobs) != 1) exit(1);
	if (fread(bitstream, 1, picture.size, jobs) != picture.size) exit(1);
	memset(bitstream + picture.size, 0, VDEC_BITSTREAM_TAIL);
	bitstream[picture.size + 3] = 1;
	sweep_caches(n);
	start = lab_now_us();
	ret = y2_vdec_h264_start(v, &picture);
	if (!ret) {
		while (!(lab_readl(VDEC_AVLD(0x278)) >> 31) && (waited = lab_now_us() - start) < 500e3)
			usleep(500);
	}
	y2_vdec_h264_result(v, &picture);
	report(v, n, ret, picture.error, picture.complete, picture.mb_x, picture.mb_y, waited);
	return picture.frame;
}

/* The kernel's handler takes the interrupt, so the bench watches the macroblock counters. */
static unsigned run_mpeg4(struct y2_vdec *v, FILE *jobs, uint8_t *bitstream, unsigned n)
{
	static const uint8_t tail[12] = { 0, 0, 1, 0xb6, 0, 0, 1, 0xb6, 0, 0, 1, 0xb6 };
	static struct y2_vdec_mpeg4 picture;
	double start, waited = 0;
	int ret;
	if (fread(&picture, sizeof(picture), 1, jobs) != 1) exit(1);
	if (fread(bitstream, 1, picture.size, jobs) != picture.size) exit(1);
	memset(bitstream + picture.size, 0, VDEC_MPEG4_TAIL);
	memcpy(bitstream + picture.size, tail, sizeof(tail));
	sweep_caches(n);
	start = lab_now_us();
	ret = y2_vdec_mpeg4_start(v, &picture);
	while (!ret && (waited = lab_now_us() - start) < 300e3) {
		if (lab_readl(VDEC_MC(0x28)) == v->mbs_w - 1 && lab_readl(VDEC_MC(0x2c)) == v->mbs_h - 1)
			break;
		usleep(500);
	}
	usleep(2000);
	/* What the stock library reads back: the status word and where the reader stopped. Only
	 * registers the stock library touches are read: others can hang the bus. */
	v->trace[1] = lab_readl(VDEC_VLD(0x3cc));
	v->trace[2] = lab_readl(VDEC_VLD(0xfc));

	y2_vdec_mpeg4_result(v, &picture);
	report(v, n, ret, picture.error, picture.complete, picture.mb_x, picture.mb_y, waited);
	return picture.frame;
}

int main(int argc, char **argv)
{
	struct y2_vdec v = { .regs = (void *)0, .sys = (void *)0x100000 };
	struct y2_vdec_config config;
	uint8_t *bitstream, *frames[Y2_VDEC_MAX_FRAMES];
	char tag[8];
	size_t frame_size;
	unsigned n = 0;
	FILE *jobs, *out;
	int fd;
	if (argc < 3) return fprintf(stderr, "usage: vdec-lab jobs.bin frames.raw [-v]\n"), 2;
	verbose = argc > 3;
	jobs = fopen(argv[1], "rb");
	out = fopen(argv[2], "wb");
	if (!jobs || !out) return perror("open"), 1;
	if (fread(tag, 1, 8, jobs) != 8 || memcmp(tag, "Y2VJ", 4)) return 1;
	if (fread(&config, sizeof(config), 1, jobs) != 1) return 1;
	fd = open("/dev/y2-vdec", O_RDWR);
	if (fd < 0) return perror("/dev/y2-vdec"), 1;
	if (ioctl(fd, Y2_VDEC_CONFIGURE, &config)) return perror("configure"), 1;
	frame_size = config.y_size + config.c_size;
	bitstream = mmap(NULL, config.bitstream_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	for (unsigned i = 0; i < config.frames; i++)
		frames[i] = mmap(NULL, frame_size, PROT_READ, MAP_SHARED, fd, Y2_VDEC_MAP_FRAME(i) * 4096L);
	sweep_buffer = malloc(16 << 20);
	put(DEBUG "power", "1");
	reg_fd = open(DEBUG "reg", O_RDWR);
	if (reg_fd < 0) return perror("reg"), 1;
	v.cfg = config;
	v.mbs_w = config.width / 16;
	v.mbs_h = config.height / 16;
	read_buffers(&v);
	printf("%ux%u frames=%u bits=%#x frame0=%#x mv0=%#x SYS=%#x\n", config.width, config.height,
	       config.frames, v.bits.dma, v.frame[0].dma, v.mv[0].dma, lab_readl(0x100000));
	while (fread(tag, 1, 4, jobs) == 4) {
		unsigned frame;
		if (!memcmp(tag, "PIC1", 4)) frame = run_h264(&v, jobs, bitstream, n);
		else if (!memcmp(tag, "PIC4", 4)) frame = run_mpeg4(&v, jobs, bitstream, n);
		else break;
		sweep_caches(n + 7);
		fwrite(frames[frame], 1, frame_size, out);
		n++;
	}
	put(DEBUG "power", "0");
	return 0;
}
