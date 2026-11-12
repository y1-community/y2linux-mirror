// SPDX-License-Identifier: GPL-2.0-only
/* Replay decode jobs (h264_jobs.py) on /dev/y2-vdec and write every decoded frame as the hardware
 * left it: `vdec-run jobs.bin frames.raw`. Bring-up and qualification tool; runs on the player. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "../../../kernel/platform/y2-vdec.h"

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
	struct y2_vdec_config config;
	struct y2_vdec_h264 picture;
	uint8_t *bitstream, *frames[Y2_VDEC_MAX_FRAMES];
	char tag[8];
	size_t frame_size;
	unsigned n = 0, bad = 0;
	double total = 0;
	FILE *jobs, *out;
	int fd;
	if (argc != 3) return fprintf(stderr, "usage: vdec-run jobs.bin frames.raw\n"), 2;
	jobs = fopen(argv[1], "rb");
	out = fopen(argv[2], "wb");
	if (!jobs || !out) return perror("open"), 1;
	if (fread(tag, 1, 8, jobs) != 8 || memcmp(tag, "Y2VJ", 4)) return fprintf(stderr, "not a job file\n"), 1;
	if (fread(&config, sizeof(config), 1, jobs) != 1) return 1;
	fd = open("/dev/y2-vdec", O_RDWR | O_CLOEXEC);
	if (fd < 0) return perror("/dev/y2-vdec"), 1;
	if (ioctl(fd, Y2_VDEC_CONFIGURE, &config)) return perror("configure"), 1;
	frame_size = config.y_size + config.c_size;
	printf("%ux%u frames=%u y=%u c=%u\n", config.width, config.height, config.frames, config.y_size,
	       config.c_size);
	bitstream = mmap(NULL, config.bitstream_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
			 Y2_VDEC_MAP_BITSTREAM * 4096L);
	if (bitstream == MAP_FAILED) return perror("map bitstream"), 1;
	for (unsigned i = 0; i < config.frames; i++) {
		frames[i] = mmap(NULL, frame_size, PROT_READ, MAP_SHARED, fd, Y2_VDEC_MAP_FRAME(i) * 4096L);
		if (frames[i] == MAP_FAILED) return perror("map frame"), 1;
	}
	while (fread(tag, 1, 4, jobs) == 4 && !memcmp(tag, "PIC1", 4)) {
		double start;
		if (fread(&picture, sizeof(picture), 1, jobs) != 1) break;
		if (picture.size > config.bitstream_size || fread(bitstream, 1, picture.size, jobs) != picture.size)
			break;
		start = now_ms();
		if (ioctl(fd, Y2_VDEC_H264_DECODE, &picture)) {
			fprintf(stderr, "picture %u: %s\n", n, strerror(errno));
			return 1;
		}
		total += now_ms() - start;
		if (picture.error || !picture.complete) bad++;
		printf("picture %u frame=%u poc=%d error=%#x complete=%u mb=%u,%u %.2f ms\n", n,
		       picture.frame, picture.top_poc < picture.bottom_poc ? picture.top_poc : picture.bottom_poc,
		       picture.error, picture.complete, picture.mb_x, picture.mb_y, now_ms() - start);
		if (fwrite(frames[picture.frame], 1, frame_size, out) != frame_size) return perror("write"), 1;
		n++;
	}
	printf("%u pictures, %u bad, %.2f ms each\n", n, bad, n ? total / n : 0);
	return bad != 0;
}
