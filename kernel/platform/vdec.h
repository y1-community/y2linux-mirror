/* SPDX-License-Identifier: GPL-2.0-only */
/* MT6582 video decoder: what the device (vdec.c) and the codecs (vdec-h264.c) share.
 * Register behaviour: docs/knowledge/video-decoder.md. */
#ifndef Y2_VDEC_H
#define Y2_VDEC_H
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "y2-vdec.h"

/* Register blocks of the decoder, offsets from 0x16020000. */
#define VDEC_MISC(r) (0x0000 + (r))
#define VDEC_VLD(r) (0x1000 + (r))
#define VDEC_TOP(r) (0x1800 + (r))
#define VDEC_MC(r) (0x2000 + (r))
#define VDEC_AVLD(r) (0x3000 + (r))
#define VDEC_AMV(r) (0x4000 + (r))
#define VDEC_PP(r) (0x5000 + (r))

/* Sizes the stock library gives the hardware's working memory. */
#define VDEC_BITSTREAM_SIZE (2U << 20)
#define VDEC_BITSTREAM_TAIL 0x104U /* terminator and the read-ahead the hardware is told about */
#define VDEC_MPEG4_TAIL 0x400U     /* three start codes and the read-ahead of the MPEG-4 reader */
#define VDEC_WORK_SIZE 0x8000U
#define VDEC_MPEG4_WORK_SIZE 0x20000U
#define VDEC_WORK_A_SIZE 0x1e000U
#define VDEC_WORK_B_SIZE 0xf000U

struct y2_vdec_buf {
	struct page *page;
	void *va;
	dma_addr_t dma;
	size_t size;
};

struct y2_vdec {
	struct device *dev;
	void __iomem *sys, *regs;
	struct clk *bus;
	int irq;
	struct miscdevice misc;
	struct mutex lock;          /* everything below, and the hardware */
	struct completion done;
	struct delayed_work idle;
	unsigned long opened;
	bool powered, broken;
	struct y2_vdec_config cfg;  /* cfg.frames == 0: not configured */
	unsigned mbs_w, mbs_h;
	struct y2_vdec_buf bits, work, work_a, work_b;
	/* Motion vectors: of every frame (H.264); MPEG-4 keeps the last reference's in mv[0] and its
	 * second side buffer in mv[1]. */
	struct y2_vdec_buf frame[Y2_VDEC_MAX_FRAMES], mv[Y2_VDEC_MAX_FRAMES];
	dma_addr_t last_y;          /* luma of the picture decoded before this one */
	/* Counters for sysfs and error reports. */
	unsigned pictures, errors, timeouts, power_ons, last_error, last_mux;
	atomic_t maps;              /* user mappings of the buffers */
	struct dentry *debug;
	u32 debug_reg;
	bool debug_hold;            /* powered by hand: the idle timer leaves it on */
	u32 trace[8];               /* what the last picture's start saw, for bring-up */
};

static inline u32 vdec_read(struct y2_vdec *v, unsigned reg)
{
	return readl(v->regs + reg);
}
static inline void vdec_write(struct y2_vdec *v, unsigned reg, u32 value)
{
	writel(value, v->regs + reg);
}
static inline void vdec_set(struct y2_vdec *v, unsigned reg, u32 bits)
{
	vdec_write(v, reg, vdec_read(v, reg) | bits);
}

/* Program one picture and start the hardware; the caller then waits for the interrupt. */
int y2_vdec_h264_start(struct y2_vdec *v, const struct y2_vdec_h264 *p);
/* Read the result of the picture into `p`. */
void y2_vdec_h264_result(struct y2_vdec *v, struct y2_vdec_h264 *p);
int y2_vdec_mpeg4_start(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p);
void y2_vdec_mpeg4_result(struct y2_vdec *v, struct y2_vdec_mpeg4 *p);
#endif
