// SPDX-License-Identifier: GPL-2.0-only
/* MT6582 video decoder: one MPEG-4 Part 2 picture.
 *
 * The register sequence is the one the stock firmware's libvcodecdrv.so runs for this chip
 * (mpeg4_part2_risc_settings and eBufInitBS), read out in docs/knowledge/video-decoder.md. The caller
 * has parsed the VOL and VOP headers; the hardware starts at the first macroblock and reads the video
 * packets of the picture itself. Two buffers carry state from a reference picture to the B pictures
 * between it and the next (motion vectors and the map of macroblocks that were not coded), so they
 * belong to the stream and outlive the decoder's power.
 *
 * Unlike the stock library, the picture is taken from motion compensation directly: the stock
 * library shows a second, deblocked copy written by the post-processing stage, which costs a frame
 * of memory traffic and is not what the standard decodes. What the hardware cannot do is in
 * docs/knowledge/video-decoder.md (interlace in B pictures, a video packet in front of a skipped
 * macroblock of a B picture); the caller keeps such streams away. */
#include <linux/delay.h>
#include <linux/iopoll.h>
#include "vdec.h"

#define MPEG4_MISC_MODE 0x3e5a1108U
static void mpeg4_mask(struct y2_vdec *v, unsigned reg, u32 keep, u32 value)
{
	vdec_write(v, reg, (vdec_read(v, reg) & keep) | value);
}

static bool mpeg4_b(const struct y2_vdec_mpeg4 *p) { return p->coding_type == Y2_VDEC_MPEG4_B; }
static u32 mpeg4_flag(const struct y2_vdec_mpeg4 *p, u16 flag) { return !!(p->flags & flag); }

static void mpeg4_reset(struct y2_vdec *v)
{
	vdec_write(v, VDEC_VLD(0x108), 0x101);
	vdec_write(v, VDEC_MISC(0xc8), 0xfd);
	vdec_write(v, VDEC_MISC(0xcc), MPEG4_MISC_MODE);
	vdec_write(v, VDEC_MISC(0xf4), 0);
	vdec_write(v, VDEC_MISC(0x84), 1);
	vdec_write(v, VDEC_VLD(0x108), 0);
	writel(1, v->sys + 0x18);
	vdec_write(v, VDEC_MISC(0xec), 1);
	vdec_write(v, VDEC_MISC(0x4), 1);
}

/* Scaled distances for direct mode: 14-bit fractions of the distance between the references. */
static u32 mpeg4_ratio(s32 trd, s32 trb, s32 of, s32 shift)
{
	if (!of) return 0;
	return (u32)(((trd - trb) << 14) / of & 0xffff) | (u32)(((trb + shift) << 14) / of) << 16;
}

static void mpeg4_write_times(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	s32 trd = p->trd, trb = p->trb, trdi = p->trd_field, trbi = p->trb_field;
	vdec_write(v, VDEC_AMV(0x2e0), (trd & 0xffff) | (u32)trb << 16);
	vdec_write(v, VDEC_AMV(0x2e4), (trdi & 0xffff) | (u32)trbi << 16);
	vdec_write(v, VDEC_AMV(0x2e8), ((trdi + 1) & 0xffff) | (u32)(trbi + 1) << 16);
	vdec_write(v, VDEC_AMV(0x2ec), ((trdi - 1) & 0xffff) | (u32)(trbi - 1) << 16);
	vdec_write(v, VDEC_AMV(0x2f0), mpeg4_ratio(trd, trb, trd, 0));
	vdec_write(v, VDEC_AMV(0x2f4), mpeg4_ratio(trdi, trbi, trdi, 0));
	vdec_write(v, VDEC_AMV(0x2f8), mpeg4_ratio(trdi, trbi, trdi + 1, 1));
	vdec_write(v, VDEC_AMV(0x2fc), mpeg4_ratio(trdi, trbi, trdi - 1, -1));
}

static void mpeg4_write_frames(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	u32 chroma = v->cfg.y_size;
	u32 now = v->frame[p->frame].dma, forward = v->frame[p->forward].dma;
	/* Reference 1 is the forward one, reference 2 the backward one; an I or P picture is itself
	 * the next backward reference and is decoded into that place. */
	u32 second = mpeg4_b(p) ? v->frame[p->backward].dma : now;
	vdec_write(v, VDEC_MC(0x0), forward >> 9);
	vdec_write(v, VDEC_MC(0x4), (forward + chroma) >> 8);
	vdec_write(v, VDEC_MC(0x8), second >> 9);
	vdec_write(v, VDEC_MC(0xc), (second + chroma) >> 8);
	vdec_write(v, VDEC_MC(0x9c), now >> 9);
	vdec_write(v, VDEC_MC(0xa0), (now + chroma) >> 8);
	/* Decode into reference 2 (1) or into the B place (4); reference 1 is the forward one. */
	vdec_write(v, VDEC_MC(0x24), mpeg4_b(p) ? 4 : 1);
	vdec_write(v, VDEC_MC(0x18), 0);
	mpeg4_mask(v, VDEC_MC(0xa2c), ~BIT(28), p->width <= 176 ? BIT(28) : 0);
	mpeg4_mask(v, VDEC_MC(0x920), ~BIT(24), BIT(24));
	vdec_write(v, VDEC_MC(0x980), v->mbs_w);
	mpeg4_mask(v, VDEC_PP(0x3c), ~BIT(28), BIT(28));
	/* Post-processing stays off; its registers are otherwise as the stock library leaves them. */
	vdec_write(v, VDEC_MC(0x220), 0);
	vdec_write(v, VDEC_MC(0x224), now >> 9);
	vdec_write(v, VDEC_MC(0x228), (now + chroma) >> 8);
	vdec_write(v, VDEC_MC(0x22c), v->mbs_w);
	vdec_write(v, VDEC_MC(0x700), 0);
	vdec_write(v, VDEC_MC(0x238), 3);
	vdec_write(v, VDEC_MC(0x250), 0);
	mpeg4_mask(v, VDEC_MC(0x658), ~1U, 0);
	vdec_write(v, VDEC_MC(0x260), v->mbs_w - 1);
	vdec_write(v, VDEC_MC(0x264), v->mbs_h - 1);
	vdec_write(v, VDEC_MC(0xa24), v->work_a.dma);
}

/* Bytes of the map of macroblocks that were not coded, as the stock library counts them for this
 * chip; the hardware saves and loads a sixteenth of it in 64-bit words. */
static u32 mpeg4_map_bytes(const struct y2_vdec *v)
{
	return ((v->mbs_w * v->mbs_h + 0x80) & ~0x7fU) >> 2;
}

static void mpeg4_write_matrix(struct y2_vdec *v, const u8 *matrix, u32 at, u32 load)
{
	unsigned i;
	for (i = 0; i < 16; i++) {
		vdec_write(v, VDEC_VLD(0x260), at + 4 * i);
		vdec_write(v, VDEC_VLD(0x264), (u32)matrix[4 * i] << 24 | matrix[4 * i + 1] << 16 |
						       matrix[4 * i + 2] << 8 | matrix[4 * i + 3]);
	}
	vdec_write(v, VDEC_VLD(0x104), load);
}

static void mpeg4_write_parameters(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	u32 quarter = mpeg4_flag(p, Y2_VDEC_MPEG4_QUARTER_SAMPLE);
	u32 top_first = mpeg4_flag(p, Y2_VDEC_MPEG4_TOP_FIELD_FIRST);
	/* No macroblock of a B picture is skipped for its backward reference when that is an I picture. */
	u32 backward = mpeg4_b(p) && mpeg4_flag(p, Y2_VDEC_MPEG4_BACKWARD_INTRA) ? 3 : 0;
	u32 row = v->cfg.width * 6;
	mpeg4_write_times(v, p);
	vdec_write(v, VDEC_AMV(0x2d8), quarter | top_first << 1);
	vdec_write(v, VDEC_AMV(0x2dc), 0);
	vdec_write(v, VDEC_AMV(0x20c), v->mv[0].dma >> 4);
	mpeg4_mask(v, VDEC_AMV(0x218), ~1U, backward & 1);
	vdec_write(v, VDEC_MC(0x1fc), 1);
	vdec_write(v, VDEC_MC(0x200), quarter);
	vdec_write(v, VDEC_MC(0x204), mpeg4_flag(p, Y2_VDEC_MPEG4_ROUNDING));
	vdec_write(v, VDEC_MC(0x208), v->cfg.width);
	vdec_write(v, VDEC_MC(0x20c), v->cfg.height);
	vdec_write(v, VDEC_MC(0x21c), 0);
	mpeg4_write_frames(v, p);
	if (p->flags & Y2_VDEC_MPEG4_MPEG_QUANT) {
		mpeg4_write_matrix(v, p->intra_matrix, 0, 0x100);
		mpeg4_write_matrix(v, p->inter_matrix, 0x40, 0x200);
	}
	mpeg4_mask(v, VDEC_VLD(0x1e8), 0xfffffefc, 0x100 | backward);
	vdec_write(v, VDEC_VLD(0x1c8), 0x10001);
	vdec_write(v, VDEC_VLD(0x88), top_first << 26 | (u32)p->coding_type << 21 |
				      mpeg4_flag(p, Y2_VDEC_MPEG4_ALTERNATE_SCAN) << 17 |
				      (u32)p->fcode_forward << 12 | (u32)p->fcode_backward << 4);
	mpeg4_mask(v, VDEC_VLD(0x1c4), 0xf8070000,
		   mpeg4_flag(p, Y2_VDEC_MPEG4_RESYNC_DISABLE) << 26 | quarter << 25 |
		   mpeg4_flag(p, Y2_VDEC_MPEG4_MPEG_QUANT) << 24 |
		   mpeg4_flag(p, Y2_VDEC_MPEG4_INTERLACED) << 22 |
		   mpeg4_flag(p, Y2_VDEC_MPEG4_DATA_PARTITIONED) << 21 |
		   (mpeg4_b(p) ? 0 : mpeg4_flag(p, Y2_VDEC_MPEG4_REVERSIBLE_VLC) << 20) |
		   p->time_increment_resolution);
	vdec_write(v, VDEC_VLD(0x1d8), 0);
	vdec_write(v, VDEC_VLD(0x1d4), p->quant | (u32)p->intra_dc_vlc_thr << 8);
	vdec_write(v, VDEC_VLD(0x90), 0x1ff);
	vdec_write(v, VDEC_VLD(0x94), 0x1ff);
	vdec_write(v, VDEC_TOP(0x68), (v->mbs_w - 1) | (v->mbs_h - 1) << 16);
	vdec_write(v, VDEC_TOP(0x70), (p->width + 15) | (u32)(p->height + 15) << 16);
	vdec_write(v, VDEC_TOP(0x28), v->work.dma);
	vdec_write(v, VDEC_VLD(0x2d4), v->work_b.dma);
	vdec_write(v, VDEC_VLD(0x2d8), v->mv[1].dma);
	if (p->width <= 176) {
		vdec_write(v, VDEC_VLD(0x2dc), mpeg4_map_bytes(v) >> 4 | (v->cfg.width >> 1) << 16);
		mpeg4_mask(v, VDEC_PP(0x800), ~BIT(8), BIT(8));
		mpeg4_mask(v, VDEC_TOP(0x2c), ~BIT(30), BIT(30));
		mpeg4_mask(v, VDEC_TOP(0x40), ~0x30U, 0x20);
		mpeg4_mask(v, VDEC_VLD(0x2d0), ~0x84U, 0x84);
	} else {
		vdec_write(v, VDEC_VLD(0x2dc), mpeg4_map_bytes(v) >> 4 | (row >> 4) << 16);
		vdec_write(v, VDEC_TOP(0x94), v->work.dma + row - 0x10);
		mpeg4_mask(v, VDEC_TOP(0x2c), ~BIT(30), 0);
		mpeg4_mask(v, VDEC_TOP(0x40), ~0x30U, 0);
		mpeg4_mask(v, VDEC_VLD(0x2d0), ~4U, 0x80);
		mpeg4_mask(v, VDEC_VLD(0x2e0), ~2U, 2);
	}
	vdec_write(v, VDEC_VLD(0x1fc), 0x31);
	mpeg4_mask(v, VDEC_VLD(0x2f8), 0xfffff7bf, 0);
	vdec_write(v, VDEC_VLD(0x1d0), 0x100);
}

/* eBufInitBS: point the bit reader at the buffer and step over the VOP header. The reader steps at
 * most 24 bits at a time, so a long way is shortened by starting at a later 64-byte block. */
static int mpeg4_feed(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	u32 blocks = p->header_bits > 512 ? p->header_bits >> 9 : 0;
	u32 skip = p->header_bits - blocks * 512;
	u32 start = v->bits.dma + blocks * 64, end = v->bits.dma + p->size, value;
	vdec_read(v, VDEC_VLD(0xf4));
	vdec_write(v, VDEC_VLD(0xb4), start >> 6);
	vdec_write(v, VDEC_VLD(0xb8), (end >> 6) + 0xc);
	vdec_write(v, VDEC_VLD(0xb0), start);
	vdec_write(v, VDEC_VLD(0x110), end + 0x300);
	vdec_write(v, VDEC_VLD(0x108), 0x100);
	vdec_write(v, VDEC_VLD(0x108), 0);
	mpeg4_mask(v, VDEC_VLD(0x1d0), ~BIT(8), BIT(8));
	vdec_write(v, VDEC_VLD(0x8c), 0x100000);
	if (readl_poll_timeout_atomic(v->regs + VDEC_VLD(0xe8), value, value & 1, 1, 20000))
		return -EIO;
	vdec_write(v, VDEC_VLD(0x8c), 0x800000);
	vdec_read(v, VDEC_VLD(0xf0));
	for (; skip >= 24; skip -= 24) vdec_read(v, VDEC_VLD(4 * 24));
	if (skip) vdec_read(v, VDEC_VLD(4 * skip));
	v->trace[0] = vdec_read(v, VDEC_VLD(0xf0));
	return 0;
}

static void mpeg4_start(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	vdec_write(v, VDEC_VLD(0x1cc), 1);
	vdec_write(v, VDEC_VLD(0x1cc), 0);
	vdec_write(v, VDEC_VLD(0x1d0), 1);
	vdec_write(v, VDEC_VLD(0x1d0), 0);
	vdec_write(v, VDEC_VLD(0x8c), 0x8000000);
	vdec_write(v, VDEC_VLD(0x1d0), 0);
	vdec_write(v, VDEC_TOP(0x44), ~0U);
	vdec_set(v, VDEC_VLD(0x84), 0x600);
	vdec_write(v, VDEC_VLD(0xdc), 0x10000);
	if (p->width > 176) {
		mpeg4_mask(v, VDEC_PP(0x754), ~1U, 1);
		mpeg4_mask(v, VDEC_PP(0x804), ~BIT(4), 0);
	}
	if (p->coding_type == Y2_VDEC_MPEG4_I) {
		mpeg4_mask(v, VDEC_VLD(0x2ec), ~3U, 0);
		mpeg4_mask(v, VDEC_VLD(0x2f0), 0xfc00fc00, 0x103ff);
	} else {
		mpeg4_mask(v, VDEC_VLD(0x2ec), 0xffffffb0, 0x45);
	}
	if (p->flags & Y2_VDEC_MPEG4_DATA_PARTITIONED) mpeg4_mask(v, VDEC_VLD(0x2ec), ~BIT(17), BIT(17));
	vdec_write(v, VDEC_VLD(0x1e4), 1);
	vdec_write(v, VDEC_VLD(0x1e4), 0);
}

static int mpeg4_valid(const struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	if (v->cfg.codec != Y2_VDEC_CODEC_MPEG4) return -EINVAL;
	if (p->frame >= v->cfg.frames || p->forward >= v->cfg.frames || p->backward >= v->cfg.frames)
		return -EINVAL;
	if (p->coding_type > Y2_VDEC_MPEG4_B) return -EINVAL;
	if (!p->size || p->size > v->bits.size - VDEC_MPEG4_TAIL) return -EINVAL;
	if (p->header_bits >= p->size * 8) return -EINVAL;
	if (!p->width || !p->height || ALIGN(p->width, 16) != v->cfg.width ||
	    ALIGN(p->height, 16) != v->cfg.height)
		return -EINVAL;
	if (!p->quant || p->quant > 31 || p->intra_dc_vlc_thr > 7) return -EINVAL;
	if (p->fcode_forward > 7 || p->fcode_backward > 7) return -EINVAL;
	return 0;
}

int y2_vdec_mpeg4_start(struct y2_vdec *v, const struct y2_vdec_mpeg4 *p)
{
	int ret = mpeg4_valid(v, p);
	if (ret) return ret;
	mpeg4_reset(v);
	mpeg4_write_parameters(v, p);
	ret = mpeg4_feed(v, p);
	if (ret) return ret;
	mpeg4_start(v, p);
	return 0;
}

void y2_vdec_mpeg4_result(struct y2_vdec *v, struct y2_vdec_mpeg4 *p)
{
	vdec_write(v, VDEC_MISC(0xc8), 0xfd);
	vdec_write(v, VDEC_MISC(0xcc), 0x3e6a1108);
	writel(1, v->sys);
	p->error = vdec_read(v, VDEC_VLD(0x3cc)) & BIT(8);
	p->mb_x = vdec_read(v, VDEC_MC(0x28));
	p->mb_y = vdec_read(v, VDEC_MC(0x2c));
	p->complete = p->mb_x == v->mbs_w - 1 && p->mb_y == v->mbs_h - 1;
	v->last_y = v->frame[p->frame].dma;
}
