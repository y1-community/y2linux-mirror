/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/* /dev/y2-vdec: the MT6582 hardware video decoder of the Innioasis Y2.
 *
 * One process at a time opens the device, sizes the buffers (CONFIGURE), maps the bitstream buffer and
 * the frames, and asks for one picture per DECODE call. The caller parses the stream (parameter sets,
 * picture order, reference marking); the driver programs the hardware and returns when the picture is in
 * the frame. Userspace never sees a register or a physical address.
 *
 * Frames are NV12 in the decoder's block layout: luma in 16x32 blocks, chroma (CbCr interleaved) in 16x16
 * blocks, blocks in raster order, each block row-major. Luma starts at offset 0 of the frame mapping and
 * is `y_size` bytes; chroma follows and is `c_size` bytes. */
#ifndef Y2_VDEC_UAPI_H
#define Y2_VDEC_UAPI_H
#include <linux/ioctl.h>
#include <linux/types.h>

#define Y2_VDEC_MAX_FRAMES 24
#define Y2_VDEC_CODEC_H264 1
#define Y2_VDEC_CODEC_MPEG4 2

/* mmap offsets, in pages: the bitstream buffer (read/write) and frame `n` (read only). */
#define Y2_VDEC_MAP_BITSTREAM 0
#define Y2_VDEC_MAP_FRAME(n) (1 + (n))

struct y2_vdec_config {
	__u32 codec;          /* in: Y2_VDEC_CODEC_* */
	__u32 width, height;  /* in: coded size, multiples of 16 */
	__u32 frames;         /* in: number of frames, 1..Y2_VDEC_MAX_FRAMES */
	__u32 y_size, c_size; /* out: bytes of luma and of chroma in one frame */
	__u32 bitstream_size; /* out: bytes of the bitstream buffer */
	__u32 reserved;
};

#define Y2_VDEC_H264_REF_LONG 0x01
#define Y2_VDEC_H264_REF_TOP 0x02    /* its top field is a reference */
#define Y2_VDEC_H264_REF_BOTTOM 0x04 /* its bottom field is a reference */
#define Y2_VDEC_H264_REF_FIELDS 0x08 /* it was coded as two field pictures */
#define Y2_VDEC_H264_REF_MBAFF 0x10  /* it was coded as a frame with field macroblock pairs */
struct y2_vdec_h264_ref {
	__u8 frame;      /* index of the frame holding the reference */
	__u8 flags;      /* Y2_VDEC_H264_REF_*; a frame is a reference with both fields */
	__u16 number;    /* frame_num, or LongTermFrameIdx of a long-term reference */
	__s32 top_poc, bottom_poc;
};

#define Y2_VDEC_H264_CABAC 0x0001
#define Y2_VDEC_H264_PIC_ORDER_PRESENT 0x0002
#define Y2_VDEC_H264_WEIGHTED_PRED 0x0004
#define Y2_VDEC_H264_DEBLOCKING_CONTROL 0x0008
#define Y2_VDEC_H264_CONSTRAINED_INTRA 0x0010
#define Y2_VDEC_H264_TRANSFORM_8X8 0x0020
#define Y2_VDEC_H264_REDUNDANT_PIC_CNT 0x0040
#define Y2_VDEC_H264_SCALING 0x0080
#define Y2_VDEC_H264_DELTA_POC_ZERO 0x0100
#define Y2_VDEC_H264_DIRECT_8X8 0x0200
#define Y2_VDEC_H264_FIELDS 0x0400 /* the stream may hold fields (frame_mbs_only_flag is 0) */
#define Y2_VDEC_H264_MBAFF 0x0800  /* mb_adaptive_frame_field_flag */

#define Y2_VDEC_H264_TOP_FIELD 1
#define Y2_VDEC_H264_BOTTOM_FIELD 2
#define Y2_VDEC_H264_FRAME 3

/* One H.264 picture (4:2:0, 8 bit): a frame, or one field of a frame (two calls into the same
 * frame). The bitstream buffer holds the picture's slice NAL units, each behind a 00 00 01 start
 * code, `size` bytes in all. */
struct y2_vdec_h264 {
	__u32 size;
	__u8 frame;        /* frame to decode into */
	__u8 nal_ref_idc, nal_unit_type;
	__u8 refs;         /* entries used in `ref` */
	__u16 flags;       /* Y2_VDEC_H264_* */
	__u16 frame_num;
	__s32 top_poc, bottom_poc;
	/* Sequence parameter set. */
	__u8 log2_max_frame_num, poc_type, log2_max_poc_lsb, max_ref_frames;
	/* Picture parameter set. */
	__u8 weighted_bipred_idc, ref_l0_default, ref_l1_default; /* counts, not minus one */
	__s8 pic_init_qp, chroma_qp_offset, second_chroma_qp_offset; /* pic_init_qp: minus 26 */
	__u8 structure;    /* Y2_VDEC_H264_FRAME, _TOP_FIELD or _BOTTOM_FIELD */
	__u8 reserved;
	struct y2_vdec_h264_ref ref[16];
	__u8 scaling4[6][16], scaling8[2][64]; /* raster order; read with Y2_VDEC_H264_SCALING */
	/* Result. */
	__u32 error;       /* the hardware's error word, 0 when the picture is good */
	__u32 complete;    /* 1 when the hardware reports the whole picture */
	__u32 mb_x, mb_y;  /* macroblock the hardware stopped at */
};

#define Y2_VDEC_MPEG4_INTERLACED 0x0001
#define Y2_VDEC_MPEG4_TOP_FIELD_FIRST 0x0002
#define Y2_VDEC_MPEG4_ALTERNATE_SCAN 0x0004
#define Y2_VDEC_MPEG4_QUARTER_SAMPLE 0x0008
#define Y2_VDEC_MPEG4_MPEG_QUANT 0x0010      /* quant_type 1: `intra_matrix` and `inter_matrix` are read */
#define Y2_VDEC_MPEG4_RESYNC_DISABLE 0x0020  /* resync_marker_disable */
#define Y2_VDEC_MPEG4_DATA_PARTITIONED 0x0040
#define Y2_VDEC_MPEG4_REVERSIBLE_VLC 0x0080
#define Y2_VDEC_MPEG4_ROUNDING 0x0100        /* vop_rounding_type */
#define Y2_VDEC_MPEG4_BACKWARD_INTRA 0x0200  /* the backward reference of this B picture is an I picture */

#define Y2_VDEC_MPEG4_I 0
#define Y2_VDEC_MPEG4_P 1
#define Y2_VDEC_MPEG4_B 2

/* One MPEG-4 Part 2 picture (Simple and Advanced Simple profile without sprites, a rectangular
 * 4:2:0 8-bit VOP). The bitstream buffer holds the VOP from any byte up to its end, `size` bytes;
 * the first macroblock starts `header_bits` bits into it. The caller has parsed the headers. */
struct y2_vdec_mpeg4 {
	__u32 size;
	__u32 header_bits;
	__u8 frame;              /* frame to decode into */
	__u8 forward, backward;  /* reference frames: forward for P and B, backward for B */
	__u8 coding_type;        /* Y2_VDEC_MPEG4_I, _P or _B */
	__u16 flags;             /* Y2_VDEC_MPEG4_* */
	__u16 time_increment_resolution;
	__u16 width, height;     /* of the picture, at most the configured size */
	__u8 quant, intra_dc_vlc_thr, fcode_forward, fcode_backward;
	/* Ticks between the two references and from the forward reference to this B picture, counted
	 * in frames and (interlaced) in fields. */
	__s32 trd, trb, trd_field, trb_field;
	__u8 intra_matrix[64], inter_matrix[64]; /* zigzag order, as in the stream */
	/* Result. */
	__u32 error;       /* the hardware's error word, 0 when the picture is good */
	__u32 complete;    /* 1 when the hardware reports the whole picture */
	__u32 mb_x, mb_y;  /* macroblock the hardware stopped at */
};

#define Y2_VDEC_CONFIGURE _IOWR('y', 1, struct y2_vdec_config)
#define Y2_VDEC_H264_DECODE _IOWR('y', 2, struct y2_vdec_h264)
#define Y2_VDEC_MPEG4_DECODE _IOWR('y', 3, struct y2_vdec_mpeg4)
#endif
