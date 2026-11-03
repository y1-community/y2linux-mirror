/* SPDX-License-Identifier: GPL-2.0-only */
/* MT6582 DIS (display/MMSYS) MTCMOS: exact Y2 FM kernel spm_mtcmos_ctrl_disp
 * at 0xc00350a8 (REA/Ghidra assembly, POWER-FINAL-16 ledger F2). Stock powers
 * DIS off at every screen-off through its clock manager; Y2 Linux never did,
 * so DIS stayed on through every sleep (PWR_STATUS 0x314c, bit 3).
 *
 * Off: INFRA TOPAXI_PROT_EN bit 1 + ack (caller, y2_ccf_disp_protect), then
 *      SRAM_PDN 0xf00, ISO, RST_B=0 + CLK_DIS, PWR_ON/PWR_ON_S=0, status 0.
 * On:  PWR_ON, PWR_ON_S, status 1, CLK_DIS=0, ISO=0, RST_B=1, SRAM_PDN=0,
 *      then the caller releases bus protection.
 * Stock has no SRAM acknowledgement for DIS and no timeout; Y2 bounds every
 * poll. The caller serializes SPM IO and has quiesced every DIS master. */
#ifndef Y2_DISP_POLICY_H
#define Y2_DISP_POLICY_H
#include "spm-policy.h"
#define Y2_DIS_PWR_CON      0x23c
#define Y2_DIS_STATUS       0x8U
#define Y2_DIS_PROTECT_MASK 0x2U /* INFRA TOPAXI_PROT_EN/PROT_STA1 bit 1 */

static inline int y2_dis_power_down(const struct y2_spm_io *io)
{
	int ret;
	io->write(io->context, 0, 0x0b160001);
	y2_spm_update(io, Y2_DIS_PWR_CON, 0xf00, 0xf00);
	y2_spm_update(io, Y2_DIS_PWR_CON, 2, 2);
	y2_spm_update(io, Y2_DIS_PWR_CON, 0x11, 0x10);
	y2_spm_update(io, Y2_DIS_PWR_CON, 0xc, 0);
	ret = y2_spm_poll(io, 0x60c, Y2_DIS_STATUS, 0);
	if (!ret) ret = y2_spm_poll(io, 0x610, Y2_DIS_STATUS, 0);
	return ret;
}

static inline int y2_dis_power_up(const struct y2_spm_io *io)
{
	int ret;
	io->write(io->context, 0, 0x0b160001);
	y2_spm_update(io, Y2_DIS_PWR_CON, 4, 4);
	y2_spm_update(io, Y2_DIS_PWR_CON, 8, 8);
	ret = y2_spm_poll(io, 0x60c, Y2_DIS_STATUS, Y2_DIS_STATUS);
	if (!ret) ret = y2_spm_poll(io, 0x610, Y2_DIS_STATUS, Y2_DIS_STATUS);
	if (ret) return ret;
	y2_spm_update(io, Y2_DIS_PWR_CON, 0x10, 0);
	y2_spm_update(io, Y2_DIS_PWR_CON, 2, 0);
	y2_spm_update(io, Y2_DIS_PWR_CON, 1, 1);
	y2_spm_update(io, Y2_DIS_PWR_CON, 0xf00, 0);
	return 0;
}
#endif
