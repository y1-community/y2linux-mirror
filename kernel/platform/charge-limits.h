/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_CHARGE_LIMITS_H
#define Y2_CHARGE_LIMITS_H
#include "charge-table.h"
#include "charger-policy.h"
#include "source-policy.h"
/* The single charge-current decision. Every limit that can lower the PMIC
 * selector lives here; the driver programs exactly the result and nothing else.
 *
 *   stock request   vendor source policy (source-policy.h)
 *   class ceiling   what Linux lets that source class draw
 *   deep discharge  450 mA at or below the 3.4 V precharge threshold (charger-policy.h)
 *   thermal cap     PMIC-die ladder below (no pack NTC exists on this board)
 *   owner ceiling   BAT0 constant_charge_current_max; default 650 mA because no
 *                   cell charge rating is established (docs/knowledge/m4-charging.md)
 */
/* Default owner ceiling: the highest selector exercised on hardware (650 mA). */
#define Y2_CHARGE_DEFAULT_CEILING_UA 650000U

/* PMIC die ladder. The die is not the cell, but the pack has no sensor and the
 * linear charger dissipates (Vchr - Vbat) * I in the die, so a hot die means
 * reduced current. Trip points are the stock mtktspmic ladder (50..120 C,
 * mtk_ts_pmic.c); 150 C remains the latched emergency fault in the driver. */
#define Y2_THERMAL_HYST_MC 5000
struct y2_thermal_step { int trip_mc; unsigned cap_ua; };
static const struct y2_thermal_step y2_thermal_steps[] = {
	{ 60000, 450000 }, { 70000, 200000 }, { 80000, 70000 }, { 90000, 0 },
};
#define Y2_THERMAL_LEVELS (sizeof(y2_thermal_steps) / sizeof(y2_thermal_steps[0]))

/* Level 0 = no derating. Rises as soon as a trip is reached, falls only after
 * the die is 5 C below the trip that raised it, so a boundary cannot flap. */
static inline unsigned y2_thermal_level(unsigned level, int die_mc)
{
	unsigned i;
	for (i = level; i < Y2_THERMAL_LEVELS; i++)
		if (die_mc >= y2_thermal_steps[i].trip_mc) level = i + 1;
	while (level && die_mc <= y2_thermal_steps[level - 1].trip_mc - Y2_THERMAL_HYST_MC)
		level--;
	return level;
}
static inline unsigned y2_thermal_cap_ua(unsigned level)
{
	return level ? y2_thermal_steps[level - 1].cap_ua : ~0U;
}

struct y2_charge_decision {
	unsigned stock_request_ua, class_ceiling_ua, deep_cap_ua, thermal_cap_ua;
	unsigned ceiling_ua, result_ua;
};

static inline unsigned y2_charge_decide(struct y2_charge_decision *d,
					enum y2_source_type type, int allocated_ua,
					int battery_uv, unsigned thermal_level,
					unsigned ceiling_ua)
{
	unsigned ua;
	d->stock_request_ua = y2_source_stock_request_ua(type, allocated_ua);
	d->class_ceiling_ua = y2_source_class_ceiling_ua(type);
	d->deep_cap_ua = battery_uv <= Y2_PRECHARGE_UV ? Y2_DEEP_DISCHARGE_MAX_UA : ~0U;
	d->thermal_cap_ua = y2_thermal_cap_ua(thermal_level);
	d->ceiling_ua = ceiling_ua;
	ua = d->stock_request_ua;
	if (d->class_ceiling_ua < ua) ua = d->class_ceiling_ua;
	if (d->deep_cap_ua < ua) ua = d->deep_cap_ua;
	if (d->thermal_cap_ua < ua) ua = d->thermal_cap_ua;
	if (ceiling_ua < ua) ua = ceiling_ua;
	d->result_ua = y2_cs_floor_ua(ua);
	return d->result_ua;
}
#endif
