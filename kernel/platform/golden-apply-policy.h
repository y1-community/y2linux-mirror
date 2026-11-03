/* SPDX-License-Identifier: GPL-2.0-only */
/* POWER-FINAL-16 G3b: bring the remaining mt6582_power_gs_suspend_data rows
 * to golden at system-sleep entry, and restore them before anything resumes.
 * Applied inside the SPM suspend enter callback, after every device and
 * syscore suspend (no consumer can run), with the y2_dcm_io physical IO.
 *
 * Sequences are the Y2 FM kernel's own (REA assembly, ledger G3):
 *   sdm_pll_disable_op 0xc003f4dc: [RSTB bit24 off], EN off, PWR_CON ISO on, PWR_ON off
 *   sdm_pll_enable_op  0xc0041c88: PWR_ON, 1 us, ISO off, EN mask, 10 us, [RSTB on]
 *   clkmux_disable_op  0xc003f5a4: set the mux PDN bit (enable clears it)
 *   PERI/INFRA gates: PDN_SET/PDN_CLR write-one registers, sampled restore.
 * Groups are a runtime bitmask (spm.golden_apply); 0 keeps 15's behaviour. */
#ifndef Y2_GOLDEN_APPLY_POLICY_H
#define Y2_GOLDEN_APPLY_POLICY_H
#include "dcm-policy.h"

#define Y2_GA_PLLS     0x1U
#define Y2_GA_MUXES    0x2U
#define Y2_GA_PERI     0x4U
#define Y2_GA_INFRA    0x8U
#define Y2_GA_ALL      0xfU

struct y2_ga_pll { unsigned con0, pwr, en_mask; int rstb; };
/* Stock pll table 0xc09b1678 (UNIVPLL feature 5: RSTB bit 24). MAINPLL and
 * ARMPLL feed the CPU/bus and are never touched here. */
static const struct y2_ga_pll y2_ga_plls[] = {
	{ 0x10209230, 0x1020923c, 0x1, 0 },          /* MMPLL   (DIS only) */
	{ 0x1000f800, 0x1000f80c, 0x1, 0 },          /* VENCPLL (DIS only) */
	{ 0x10209240, 0x1020924c, 0x1, 0 },          /* MSDCPLL */
	{ 0x10209220, 0x1020922c, 0xfc000001, 1 },   /* UNIVPLL */
};
#define Y2_GA_PLL_COUNT 4
/* CLK_CFG_1 CAMTG/MFG/VDEC/PWM, CLK_CFG_2 MSDC30_0/USB20/SPI (UART mux kept:
 * console and the SPM UART handshake own it), CLK_CFG_3 AUDINTBUS/AUDIO/
 * MSDC30_2/MSDC30_1: the stock clkmux table's PDN bits. */
static const unsigned y2_ga_mux_reg[3] = { 0x10000050, 0x10000060, 0x10000070 };
static const unsigned y2_ga_mux_pdn[3] = { 0x80808080, 0x80808000, 0x80808080 };
#define Y2_GA_PERI_GATES   ((1U << 1) | (1U << 10))     /* THERM, USB0 */
#define Y2_GA_PERI_MSDC    ((1U << 12) | (1U << 13) | (1U << 14))
/* Golden INFRA_PDN_STA: mask 0x91bfe7, golden 0x80bfe6. Bit 23 is PMIC_WRAP
 * (clocks.c): the SPM sleep firmware drives VPROC/LP_SEL through the wrapper,
 * so Y2 keeps it ungated. KP (bit 16, the Power-key wake) is golden-on. */
#define Y2_GA_INFRA_GOLDEN (0x80bfe6U & ~(1U << 23))

struct y2_ga_state {
	unsigned applied, pll_off, mux[3], peri_set, infra_set;
	unsigned pll_con0[Y2_GA_PLL_COUNT], pll_pwr[Y2_GA_PLL_COUNT];
};
struct y2_ga_conditions { int dis_off; };

static inline void y2_ga_pll_disable(const struct y2_dcm_io *io, const struct y2_ga_pll *p)
{
	if (p->rstb) y2_dcm_set(io, p->con0, 0x1000000, 0);
	y2_dcm_set(io, p->con0, 0x1, 0);
	y2_dcm_set(io, p->pwr, 0, 0x2);
	y2_dcm_set(io, p->pwr, 0x1, 0);
}
static inline void y2_ga_pll_enable(const struct y2_dcm_io *io, const struct y2_ga_pll *p,
				    void (*delay)(unsigned))
{
	y2_dcm_set(io, p->pwr, 0, 0x1);
	delay(1);
	y2_dcm_set(io, p->pwr, 0x2, 0);
	y2_dcm_set(io, p->con0, 0, p->en_mask);
	delay(10);
	if (p->rstb) y2_dcm_set(io, p->con0, 0, 0x1000000);
}

static inline void y2_golden_apply(const struct y2_dcm_io *io, unsigned groups,
				   const struct y2_ga_conditions *c, struct y2_ga_state *s)
{
	unsigned i, peri = io->read(io->context, 0x10003018);
	*s = (struct y2_ga_state){ 0 };
	/* Consumer first, source last: gates, then muxes, then PLLs. */
	if (groups & Y2_GA_PERI) {
		s->peri_set = Y2_GA_PERI_GATES & ~peri;
		if (s->peri_set) io->write(io->context, 0x10003008, s->peri_set);
		peri |= s->peri_set;
		s->applied |= Y2_GA_PERI;
	}
	if (groups & Y2_GA_INFRA) {
		s->infra_set = Y2_GA_INFRA_GOLDEN & ~io->read(io->context, 0x10001048);
		if (s->infra_set) io->write(io->context, 0x10001040, s->infra_set);
		s->applied |= Y2_GA_INFRA;
	}
	if (groups & Y2_GA_MUXES) {
		for (i = 0; i < 3; i++) {
			s->mux[i] = io->read(io->context, y2_ga_mux_reg[i]);
			io->write(io->context, y2_ga_mux_reg[i], s->mux[i] | y2_ga_mux_pdn[i]);
		}
		s->applied |= Y2_GA_MUXES;
	}
	if (groups & Y2_GA_PLLS) {
		for (i = 0; i < Y2_GA_PLL_COUNT; i++) {
			const struct y2_ga_pll *p = &y2_ga_plls[i];
			if (i < 2 && !c->dis_off) continue;
			if (i == 2 && (peri & Y2_GA_PERI_MSDC) != Y2_GA_PERI_MSDC) continue;
			if (i == 3 && !(peri & (1U << 10))) continue;
			s->pll_con0[i] = io->read(io->context, p->con0);
			s->pll_pwr[i] = io->read(io->context, p->pwr);
			if (!(s->pll_con0[i] & 1)) continue;
			y2_ga_pll_disable(io, p);
			s->pll_off |= 1U << i;
		}
		s->applied |= Y2_GA_PLLS;
	}
}

/* Exact reverse: sources first (PLLs), then muxes, then gates. Restores only
 * what this entry changed. */
static inline void y2_golden_restore(const struct y2_dcm_io *io, struct y2_ga_state *s,
				     void (*delay)(unsigned))
{
	unsigned i;
	for (i = Y2_GA_PLL_COUNT; i-- > 0;)
		if (s->pll_off & (1U << i)) y2_ga_pll_enable(io, &y2_ga_plls[i], delay);
	if (s->applied & Y2_GA_MUXES)
		for (i = 3; i-- > 0;)
			io->write(io->context, y2_ga_mux_reg[i], s->mux[i]);
	if ((s->applied & Y2_GA_INFRA) && s->infra_set)
		io->write(io->context, 0x10001044, s->infra_set);
	if ((s->applied & Y2_GA_PERI) && s->peri_set)
		io->write(io->context, 0x10003010, s->peri_set);
	s->applied = 0;
	s->pll_off = 0;
}
#endif
