/* SPDX-License-Identifier: GPL-2.0-only */
/* MT6582 MFG sequence: MediaTek GPL BSP and actual Y2 FM kernel
 * spm_mtcmos_ctrl_mfg at 0xc0035230. The existing SPM owner serializes IO.
 * MFG has no TOPAXI protection writes in this stock sequence. */
#ifndef Y2_GPU_POLICY_H
#define Y2_GPU_POLICY_H
#include "spm-policy.h"
/* The MTCMOS sequence MFG and VDE share in the stock kernel (spm_mtcmos_ctrl_mfg,
 * spm_mtcmos_ctrl_vdec): `reg` is the domain's PWR_CON, `status` its bit in PWR_STATUS(_S). */
static inline int y2_mtcmos_sequence(const struct y2_spm_io *io, unsigned reg, unsigned status,
				     int on)
{
	int ret;
	io->write(io->context, 0, 0x0b160001);
	if (on) {
		y2_spm_update(io, reg, 4, 4);
		y2_spm_update(io, reg, 8, 8);
		ret = y2_spm_poll(io, 0x60c, status, status);
		if (!ret) ret = y2_spm_poll(io, 0x610, status, status);
		if (ret) return ret;
		y2_spm_update(io, reg, 0x10, 0);
		y2_spm_update(io, reg, 2, 0);
		y2_spm_update(io, reg, 1, 1);
		y2_spm_update(io, reg, 0xf00, 0);
		return y2_spm_poll(io, reg, 0x1000, 0);
	}
	/* Caller has quiesced the consumer and gated its clocks. */
	y2_spm_update(io, reg, 0xf00, 0xf00);
	ret = y2_spm_poll(io, reg, 0x1000, 0x1000);
	if (ret) return ret;
	y2_spm_update(io, reg, 2, 2);
	y2_spm_update(io, reg, 0x11, 0x10);
	y2_spm_update(io, reg, 0xc, 0);
	ret = y2_spm_poll(io, 0x60c, status, 0);
	if (!ret) ret = y2_spm_poll(io, 0x610, status, 0);
	return ret;
}
static inline int y2_mfg_sequence(const struct y2_spm_io *io, int on)
{
	return y2_mtcmos_sequence(io, 0x214, 0x10, on);
}
#endif
