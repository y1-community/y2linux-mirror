/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_CHARGE_TABLE_H
#define Y2_CHARGE_TABLE_H
/* The one MT6323 CHR_CON4 CS_VTH charge-current selector table.
 *
 * Verified against the stock Y2 kernel array `CS_VTH` (0xc07572f8, 16 words,
 * units of 0.01 mA, index == register value) and the MediaTek BSP
 * `CS_VTH_PMIC[]` (charging_hw_pmic.c, pinned d53dd75c3ff7). Selector 0 is the
 * largest current. The register is a command, not a measurement.
 *
 * The stock firmware requests at most 1100 mA (select_charging_curret
 * 0xc03a1d88). Selectors 0..4 (1200..1600 mA) exist in the table but are never
 * requested by any stock source, so they are not programmable: the PMIC write
 * boundary (policy.h) and the sequencer both reject them.
 */
#define Y2_CS_VTH_ENTRIES 16U
#define Y2_CHARGE_VENDOR_MAX_UA 1100000U

static const unsigned y2_cs_vth_ua[Y2_CS_VTH_ENTRIES] = {
	1600000, 1500000, 1400000, 1300000, 1200000, 1100000, 1000000, 900000,
	800000, 700000, 650000, 550000, 450000, 300000, 200000, 70000,
};

static inline unsigned y2_cs_ua(unsigned selector)
{
	return y2_cs_vth_ua[selector & (Y2_CS_VTH_ENTRIES - 1)];
}

/* Programmable: a table entry no larger than the highest stock request. */
static inline int y2_cs_selector_permitted(unsigned selector)
{
	return selector < Y2_CS_VTH_ENTRIES && y2_cs_vth_ua[selector] <= Y2_CHARGE_VENDOR_MAX_UA;
}

/* Exact current -> selector, or -1 when it is not a permitted table entry. */
static inline int y2_cs_selector_exact(unsigned ua)
{
	unsigned i;
	for (i = 0; i < Y2_CS_VTH_ENTRIES; i++)
		if (y2_cs_vth_ua[i] == ua && y2_cs_selector_permitted(i)) return (int)i;
	return -1;
}

/* Largest permitted step that does not exceed `ua` (the BSP's
 * bmt_find_closest_level, which never rounds a request up); 0 below 70 mA. */
static inline unsigned y2_cs_floor_ua(unsigned ua)
{
	unsigned i, best = 0;
	for (i = 0; i < Y2_CS_VTH_ENTRIES; i++)
		if (y2_cs_selector_permitted(i) && y2_cs_vth_ua[i] <= ua && y2_cs_vth_ua[i] > best)
			best = y2_cs_vth_ua[i];
	return best;
}
#endif
