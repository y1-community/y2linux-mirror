/* SPDX-License-Identifier: GPL-2.0-only */
/* Stock VPROC sleep voltage for full system sleep only.
 *
 * Source: the Y2's own FM kernel PMIC_INIT_SETTING_V1 (0xc04b81f8, E2 branch),
 * identical to vendor pmic_mt6323.c "20130604_0.85v":
 *   0x212[5:4]=2  QI_VPROC_VSLEEP     0x216[1]=1    VPROC_VOSEL_CTRL (HW)
 *   0x21c[6:0]=0x17 VPROC_SFCHG_FRATE  0x21c[7]=1    VPROC_SFCHG_FEN
 *   0x21c[15]=1   VPROC_SFCHG_REN      0x222[6:0]=0x18 VPROC_VOSEL_SLEEP (0.85 V)
 *   0x230[1:0]=3  VPROC_TRANSTD        0x230[8]=1    VPROC_VSLEEP_EN
 * With VOSEL_CTRL=1 the PMIC drives VOSEL_ON (0x220) while SRCLKEN is high and
 * VOSEL_SLEEP while the SPM has released it (26 MHz off in pcm_suspend). VPROC
 * also supplies the SoC core, so this is the stock sleep leakage reduction.
 *
 * Y2 runs VPROC in software mode (VOSEL 0x21e) for its own DVFS. Here the stock
 * fields are applied only after cpuidle is paused (platform prepare_late), with
 * VOSEL_ON preset to the active selector so the switch itself changes nothing,
 * and every touched word is restored exactly at platform wake/finish, mode bit
 * first. Runtime DVFS and DORMANT therefore never see the sleep configuration.
 * VSYS is not touched: its stock sleep voltage equals its normal voltage. */
#ifndef Y2_VPROC_SLEEP_POLICY_H
#define Y2_VPROC_SLEEP_POLICY_H
#define Y2_VPROC_SLEEP_SELECTOR 0x18U  /* 700 mV + 24 * 6.25 mV = 850 mV */
#define Y2_VPROC_ACTIVE_SELECTOR 72U   /* 1.15 V, the suspend admission floor */
struct y2_vproc_field {
	unsigned reg, mask, value;
};
/* Stock fields in apply order; the HW-mode bit is always last. VOSEL_ON's
 * value is the live software selector, filled in by y2_vproc_sleep_plan(). */
#define Y2_VPROC_SLEEP_STEPS 6
static const struct y2_vproc_field y2_vproc_sleep_fields[Y2_VPROC_SLEEP_STEPS] = {
	{0x220, 0x007f, 0},
	{0x21c, 0x80ff, 0x8097},
	{0x222, 0x007f, Y2_VPROC_SLEEP_SELECTOR},
	{0x230, 0x0103, 0x0103},
	{0x212, 0x0030, 0x0020},
	{0x216, 0x0002, 0x0002},
};
struct y2_vproc_sleep {
	unsigned saved[Y2_VPROC_SLEEP_STEPS];
	unsigned planned[Y2_VPROC_SLEEP_STEPS];
	int active;
};
/* Refuse unless the PMIC is in the proven software mode at the 1.15 V floor
 * and the hardware bank already agrees; never guess a selector. */
static inline int y2_vproc_sleep_plan(struct y2_vproc_sleep *s, unsigned control,
				      unsigned software, unsigned actual)
{
	unsigned i;
	if (control & 0x2)
		return -1; /* already HW mode: not the state Y2 owns */
	if ((software & 0x7f) != Y2_VPROC_ACTIVE_SELECTOR ||
	    (actual & 0x7f) != Y2_VPROC_ACTIVE_SELECTOR)
		return -2;
	for (i = 0; i < Y2_VPROC_SLEEP_STEPS; i++)
		s->planned[i] = y2_vproc_sleep_fields[i].reg == 0x220 ?
			(software & 0x7f) : y2_vproc_sleep_fields[i].value;
	return 0;
}
/* Restore order: the reverse of apply, so the mode bit returns to software
 * before any slew, sleep-voltage or VOSEL_ON field changes back. */
static inline unsigned y2_vproc_restore_index(unsigned n)
{
	return Y2_VPROC_SLEEP_STEPS - 1 - n;
}
/* Write-policy value guards for the admitted VPROC fields (policy.h). */
static inline int y2_vproc_value_allowed(unsigned reg, unsigned value)
{
	unsigned selector = value & 0x7f;
	if (reg == 0x220)
		return selector == 72 || selector == 80 || selector == 88;
	if (reg == 0x222)
		return selector >= Y2_VPROC_SLEEP_SELECTOR && selector <= 88;
	return 1;
}
#endif
