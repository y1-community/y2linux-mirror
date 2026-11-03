/* SPDX-License-Identifier: GPL-2.0-only */
/* Read-only stock suspend golden comparison (POWER-FINAL-13 diagnostic).
 *
 * Rows: the Y2 FM kernel's own mt6582_power_gs_suspend_data (0xc0759c98,
 * 57 address/mask/golden triples; identical to vendor mt_power_gs_array.c),
 * which stock's slp_chk_golden compares right before spm_go_to_sleep().
 * Only rows in always-on or INFRA blocks that are powered and clocked at that
 * point are read: TOPCKGEN, INFRACFG_AO, PERICFG, DRAMC, DDRPHY (VENCPLL),
 * MCUSYS and APMIXEDSYS. MMSYS, MFG, IMG, VDEC, AUDIO (clock-gated) and the
 * unidentified 0x10208000 rows are skipped: reading a powered-down domain can stall the bus.
 * Rows with a zero mask carry no expectation and are skipped as well.
 * Nothing is written. */
#ifndef Y2_GOLDEN_POLICY_H
#define Y2_GOLDEN_POLICY_H
struct y2_golden_row {
	unsigned phys, mask, golden;
	const char *name;
};
static const struct y2_golden_row y2_golden_rows[] = {
	{0x10000004, 0x0000ff9f, 0x00000080, "TOPCKGEN+0x004"},
	{0x1000000c, 0x00000001, 0x00000000, "TOPCKGEN+0x00c"},
	{0x10000040, 0x87818107, 0x00810106, "CLK_CFG_0"},
	{0x10000050, 0x80808080, 0x80808080, "CLK_CFG_1"},
	{0x10000060, 0x80808080, 0x80808080, "CLK_CFG_2"},
	{0x10000070, 0x80808080, 0x80808080, "CLK_CFG_3"},
	{0x10000080, 0x000083ef, 0x00000300, "CLK_CFG_4"},
	{0x10000200, 0x000003ff, 0x000003ff, "CLK_SCP_CFG_0"},
	{0x10000204, 0x00000011, 0x00000011, "CLK_SCP_CFG_1"},
	{0x10001008, 0x0000001f, 0x00000000, "INFRACFG+0x008"},
	{0x10001010, 0x00000007, 0x00000007, "INFRACFG+0x010"},
	{0x10001014, 0x00000001, 0x00000001, "INFRACFG+0x014"},
	{0x10001048, 0x0091bfe7, 0x0080bfe6, "INFRA_PDN_STA"},
	{0x10001050, 0x00000303, 0x00000303, "INFRACFG+0x050"},
	{0x10001054, 0x0000037f, 0x00000300, "INFRACFG+0x054"},
	{0x10001058, 0x001f0f07, 0x00100000, "INFRACFG+0x058"},
	{0x10003018, 0x03ffffff, 0x02feffff, "PERI_PDN0_STA"},
	{0x10003030, 0x03ffffff, 0x03ffffff, "PERI_PDN0_MD1_STA"},
	{0x10003050, 0x00001ff3, 0x000000f3, "PERICFG+0x050"},
	{0x10003054, 0x000000ff, 0x000000f0, "PERICFG+0x054"},
	{0x10003058, 0x001f0f07, 0x00000000, "PERICFG+0x058"},
	{0x100041dc, 0x03000000, 0x03000000, "DRAMC+0x1dc"},
	{0x1000f800, 0x00000001, 0x00000000, "VENCPLL_CON0"},
	{0x1020005c, 0x00000200, 0x00000200, "MCUSYS+0x05c"},
	{0x10209004, 0x00ff03f3, 0x00000000, "AP_PLL_CON1"},
	{0x10209008, 0x00000007, 0x00000000, "AP_PLL_CON2"},
	{0x1020900c, 0x0079ce7f, 0x00780000, "APMIXED+0x00c"},
	{0x10209018, 0x00ff00ff, 0x00050001, "APMIXED+0x018"},
	{0x10209024, 0x000fffff, 0x00088888, "APMIXED+0x024"},
	{0x10209210, 0x00000001, 0x00000000, "MAINPLL_CON0"},
	{0x10209220, 0x00000001, 0x00000000, "UNIVPLL_CON0"},
	{0x10209230, 0x00000001, 0x00000000, "MMPLL_CON0"},
	{0x10209240, 0x00000001, 0x00000000, "MSDCPLL_CON0"},
	{0x1020924c, 0x00000003, 0x00000002, "MSDCPLL_PWR_CON0"},
};
#define Y2_GOLDEN_ROWS (sizeof(y2_golden_rows) / sizeof(y2_golden_rows[0]))
/* The mapped blocks, indexed by y2_golden_block(). */
struct y2_golden_block {
	unsigned phys, size;
};
static const struct y2_golden_block y2_golden_blocks[] = {
	{0x10000000, 0x1000}, {0x10001000, 0x1000}, {0x10003000, 0x1000}, {0x10004000, 0x1000},
	{0x1000f000, 0x1000}, {0x10200000, 0x1000}, {0x10209000, 0x1000},
};
#define Y2_GOLDEN_BLOCKS (sizeof(y2_golden_blocks) / sizeof(y2_golden_blocks[0]))
static inline int y2_golden_block(unsigned phys)
{
	unsigned i;
	for (i = 0; i < Y2_GOLDEN_BLOCKS; i++)
		if (phys >= y2_golden_blocks[i].phys &&
		    phys < y2_golden_blocks[i].phys + y2_golden_blocks[i].size)
			return (int)i;
	return -1;
}
static inline int y2_golden_match(const struct y2_golden_row *row, unsigned value)
{
	return (value & row->mask) == row->golden;
}
#endif
