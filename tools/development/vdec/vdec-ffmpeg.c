// SPDX-License-Identifier: GPL-2.0-only
/* Decode a file's video twice through FFmpeg, side by side, on the hardware decoder and in
 * software, and compare every picture sample by sample: `vdec-ffmpeg file [pictures]`.
 * Qualification tool for the h264_y2vdec and mpeg4_y2vdec paths; runs on the player.
 *
 * H.264 has one right answer: exit status 0 only when every picture is identical. MPEG-4 Part 2
 * leaves the inverse transform to the decoder, so the hardware is a level or two off in some
 * samples and the difference grows slowly until the next I picture: exit status 0 when no sample is
 * further off than `LOOSE`. A wrongly decoded macroblock is off by far more. */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LOOSE 16

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static enum AVPixelFormat pick_hardware(AVCodecContext *c, const enum AVPixelFormat *formats)
{
	for (const enum AVPixelFormat *f = formats; *f != AV_PIX_FMT_NONE; f++)
		if (*f == AV_PIX_FMT_DRM_PRIME) return *f;
	return formats[0];
}

struct side {
	AVFormatContext *in;
	AVCodecContext *c;
	AVPacket *packet;
	AVFrame *frame;
	int stream, draining, pictures, corrupt, hardware;
	double ms;
};

static int open_side(struct side *s, const char *file, int hardware)
{
	const AVCodec *codec;
	if (avformat_open_input(&s->in, file, NULL, NULL) || avformat_find_stream_info(s->in, NULL) < 0)
		return -1;
	s->stream = av_find_best_stream(s->in, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (s->stream < 0) return -1;
	s->c = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(s->c, s->in->streams[s->stream]->codecpar);
	if (hardware) s->c->get_format = pick_hardware;
	else s->c->thread_count = 4;
	s->packet = av_packet_alloc();
	s->frame = av_frame_alloc();
	return avcodec_open2(s->c, codec, NULL) < 0 ? -1 : 0;
}

/* The next picture in output order; 0 at the end. Only the decoder's own time is counted. */
static int next_picture(struct side *s)
{
	for (;;) {
		double start = now_ms();
		int ret = avcodec_receive_frame(s->c, s->frame);
		if (ret == AVERROR(EAGAIN) && !s->draining) {
			s->ms += now_ms() - start;
			s->draining = av_read_frame(s->in, s->packet) < 0;
			start = now_ms();
			if (s->draining) avcodec_send_packet(s->c, NULL);
			else if (s->packet->stream_index == s->stream) avcodec_send_packet(s->c, s->packet);
			s->ms += now_ms() - start;
			if (!s->draining) av_packet_unref(s->packet);
			continue;
		}
		s->ms += now_ms() - start;
		if (ret < 0) return 0;
		s->pictures++;
		s->hardware |= s->frame->format == AV_PIX_FMT_DRM_PRIME;
		s->corrupt += !!(s->frame->flags & AV_FRAME_FLAG_CORRUPT);
		return 1;
	}
}

/* One sample of the visible picture, whichever layout the frame has: plane 0 luma, 1 Cb, 2 Cr. */
static int sample(const AVFrame *f, int plane, int x, int y)
{
	if (f->format == AV_PIX_FMT_DRM_PRIME) {
		int across = f->linesize[0] / 16;
		if (!plane) return f->data[0][((y / 32 * across + x / 16) * 32 + y % 32) * 16 + x % 16];
		return f->data[1][((y / 16 * across + x / 8) * 16 + y % 16) * 16 + (x % 8) * 2 + plane - 1];
	}
	return f->data[plane][y * f->linesize[plane] + x];
}

/* The largest difference between two pictures; `off` counts the samples that differ at all. */
static int compare(const AVFrame *a, const AVFrame *b, long *off)
{
	int worst = 0;
	for (int plane = 0; plane < 3; plane++) {
		int w = plane ? a->width / 2 : a->width, h = plane ? a->height / 2 : a->height;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				int d = abs(sample(a, plane, x, y) - sample(b, plane, x, y));
				*off += !!d;
				if (d > worst) worst = d;
			}
	}
	return worst;
}

int main(int argc, char **argv)
{
	struct side hw = { 0 }, sw = { 0 };
	int limit = argc > 2 ? atoi(argv[2]) : 0, differ = 0, loose = 0, worst = 0, exact;
	long off = 0;
	double samples = 0;
	if (argc < 2) return fprintf(stderr, "usage: vdec-ffmpeg file [pictures]\n"), 2;
	av_log_set_level(AV_LOG_WARNING);
	if (open_side(&hw, argv[1], 1) || open_side(&sw, argv[1], 0))
		return fprintf(stderr, "cannot open\n"), 2;
	while ((!limit || sw.pictures < limit) && next_picture(&hw) && next_picture(&sw)) {
		long before = off;
		int most = compare(hw.frame, sw.frame, &off);
		samples += hw.frame->width * hw.frame->height * 1.5;
		differ += off != before;
		if (most > LOOSE && loose++ < 10)
			printf("picture %d: a sample is %d off\n", sw.pictures - 1, most);
		if (most > worst) worst = most;
		av_frame_unref(hw.frame);
		av_frame_unref(sw.frame);
	}
	exact = hw.c->codec_id == AV_CODEC_ID_H264;
	printf("%s %dx%d: %d pictures, %d marked corrupt; hardware %s %.2f ms, software %.2f ms each\n",
	       hw.c->codec->name, hw.c->width, hw.c->height, sw.pictures, hw.corrupt,
	       hw.hardware ? "used," : "REFUSED (software),", hw.pictures ? hw.ms / hw.pictures : 0,
	       sw.pictures ? sw.ms / sw.pictures : 0);
	printf("%d pictures differ, %d by more than %d; largest difference %d; %.3f %% of samples\n",
	       differ, loose, LOOSE, worst, samples ? off * 100.0 / samples : 0);
	return !hw.hardware || hw.pictures != sw.pictures || (exact ? differ : loose) ? 1 : 0;
}
