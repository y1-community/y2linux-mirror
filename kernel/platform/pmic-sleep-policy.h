/* SPDX-License-Identifier: GPL-2.0-only */
/* POWER-FINAL-18: bring MT6323 fields that stock has at suspend
 * (mt6323_power_gs_suspend_data, ledger F15) to their stock value inside the
 * SPM enter callback, and restore exactly the saved bits before anything
 * resumes. Field names are the Y2 FM kernel's upmu_set_* setters (register,
 * shift and mask decoded from the image):
 *
 * group 1, PMIC audio clocks (stock AudDrv_ANA_Clk_Off / AudDrv_Suspend_Clk_Off
 *   clear rg_clksq_en_aud when no analog audio user is left, ledger F16):
 *   0x102[0]  rg_clksq_en_aud      1 -> 0 (forces the 26 MHz clock square on)
 *   0x70c[4]  rg_adacck_en         1 -> 0 (PMIC audio DAC clock)
 * group 2, unused PMIC clocks gated in stock suspend:
 *   0x108[6]  rg_drv_2m_ck_pdn, 0x108[10] rg_rtc_mclk_pdn,
 *   0x108[12] rg_efuse_ck_pdn, 0x10e[5] rg_auxadc_ctl_ck_pdn   0 -> 1
 * group 4, the 1.8 V references (stock pmic_early_suspend, ledger F5):
 *   0x776[15] rg_vref18_enb, 0x778[15] rg_vref18_enb_md        0 -> 1
 * group 8, POWER-FINAL-27 VSYS buck sleep state (stock PMIC_INIT_SETTING_V1,
 *   vendor pmic_mt6323.c 3417-3424, ledger F21), apply order, mode bit last:
 *   0x242 VSYS_SFCHG_FRATE 0x23 / RRATE 0x11 / REN 1
 *   0x256 VSYS_TRANSTD 3, VSYS_VOSEL_TRANS_EN 1, VSYS_VSLEEP_EN 1
 *   0x238[5:4] QI_VSYS_VSLEEP 2, 0x23c[1] VSYS_VOSEL_CTRL 1 (hardware)
 *   The voltage never changes: VOSEL (0x244), VOSEL_ON (0x246), VOSEL_SLEEP
 *   (0x248) and the bank readback (0x24a) stay unwritable, and the group is
 *   only applied while VSYS is in software mode with all four equal
 *   (y2_ps_vsys_ready), so hardware mode selects the same 2.2 V awake and
 *   asleep; only the buck's own sleep state follows SRCLKEN as in stock.
 *
 * Deliberately not here: VEMC_3V3 off (0x508[14]) — overlay 0029 keeps the root
 * eMMC powered and in TRAN state across suspend without re-initialisation;
 * removing its supply needs a different eMMC suspend model first. The VPROC
 * sleep rows belong to vproc-sleep-policy.h; AUXADC hardware-mode/reset bits,
 * VSYS/VPA sleep and depop VCM are not adopted, nor 0x108[9]
 * rg_accdet_ck_pdn: Linux already gates the headset-detect clock that stock
 * keeps running.
 *
 * Every device is suspended when this runs; nothing uses PMIC audio, AUXADC or
 * eFuse until the restore, which precedes every resume callback. */
#ifndef Y2_PMIC_SLEEP_POLICY_H
#define Y2_PMIC_SLEEP_POLICY_H

#define Y2_PS_AUDIO   0x1U
#define Y2_PS_CLOCKS  0x2U
#define Y2_PS_VREF    0x4U
#define Y2_PS_VSYS    0x8U
#define Y2_PS_ALL     0xfU

struct y2_ps_field { unsigned short reg, mask, value, group; };
static const struct y2_ps_field y2_ps_fields[] = {
	{ 0x102, 0x0001, 0x0000, Y2_PS_AUDIO },
	{ 0x70c, 0x0010, 0x0000, Y2_PS_AUDIO },
	{ 0x108, 0x1440, 0x1440, Y2_PS_CLOCKS },
	{ 0x10e, 0x0020, 0x0020, Y2_PS_CLOCKS },
	{ 0x776, 0x8000, 0x8000, Y2_PS_VREF },
	{ 0x778, 0x8000, 0x8000, Y2_PS_VREF },
	{ 0x242, 0xff7f, 0x9123, Y2_PS_VSYS },
	{ 0x256, 0x0133, 0x0113, Y2_PS_VSYS },
	{ 0x238, 0x0030, 0x0020, Y2_PS_VSYS },
	{ 0x23c, 0x0002, 0x0002, Y2_PS_VSYS },
};
/* VSYS precondition: software mode and one selector everywhere. */
static inline int y2_ps_vsys_ready(unsigned control, unsigned vosel, unsigned on,
				   unsigned sleep, unsigned actual)
{
	vosel &= 0x7f;
	return !(control & 0x2) && (on & 0x7f) == vosel &&
	       (sleep & 0x7f) == vosel && (actual & 0x7f) == vosel;
}
#define Y2_PS_FIELDS (sizeof(y2_ps_fields) / sizeof(y2_ps_fields[0]))

struct y2_ps_io {
	void *context;
	int (*read)(void *, unsigned reg, unsigned *value);
	int (*update)(void *, unsigned reg, unsigned mask, unsigned value);
};
struct y2_ps_state {
	unsigned saved[Y2_PS_FIELDS];
	unsigned changed;	/* bit n: field n was written and must be restored */
};

/* Apply the requested groups; a field already at its stock value is left
 * alone. On the first failure everything written so far is restored and the
 * error returned: sleep then continues with the unchanged PMIC. */
static inline int y2_ps_restore(const struct y2_ps_io *io, struct y2_ps_state *s);
static inline int y2_ps_apply(const struct y2_ps_io *io, unsigned groups, struct y2_ps_state *s)
{
	unsigned i, value;
	int ret;
	s->changed = 0;
	for (i = 0; i < Y2_PS_FIELDS; i++) {
		const struct y2_ps_field *f = &y2_ps_fields[i];
		if (!(groups & f->group)) continue;
		ret = io->read(io->context, f->reg, &value);
		if (ret) goto undo;
		s->saved[i] = value & f->mask;
		if (s->saved[i] == f->value) continue;
		ret = io->update(io->context, f->reg, f->mask, f->value);
		if (ret) goto undo;	/* not written: nothing to restore for it */
		s->changed |= 1U << i;
		ret = io->read(io->context, f->reg, &value);
		if (!ret && (value & f->mask) != f->value) ret = -EIO;
		if (ret) goto undo;
	}
	return 0;
undo:
	y2_ps_restore(io, s);
	return ret;
}
/* Reverse order; every changed field is attempted even after a failure. */
static inline int y2_ps_restore(const struct y2_ps_io *io, struct y2_ps_state *s)
{
	unsigned i, value;
	int ret, first = 0;
	for (i = Y2_PS_FIELDS; i-- > 0;) {
		const struct y2_ps_field *f = &y2_ps_fields[i];
		if (!(s->changed & (1U << i))) continue;
		ret = io->update(io->context, f->reg, f->mask, s->saved[i]);
		if (!ret) ret = io->read(io->context, f->reg, &value);
		if (!ret && (value & f->mask) != s->saved[i]) ret = -EIO;
		if (ret && !first) first = ret;
		if (!ret) s->changed &= ~(1U << i);
	}
	return first;
}
#endif
