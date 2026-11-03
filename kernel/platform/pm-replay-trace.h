/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_PM_REPLAY_TRACE_H
#define Y2_PM_REPLAY_TRACE_H
#include <linux/of.h>
#include <linux/suspend.h>
#include "pm-journal.h"

/* Reuse the bounded retained PM ring for the measured Y2 replay stall. Normal
 * runtime I/O/idle never emits these notes, and no register is read or written. */
static inline void y2_pm_replay_note(struct device *dev, const char *step, int ret)
{
	if (IS_ENABLED(CONFIG_Y2_POWER) && pm_suspend_target_state != PM_SUSPEND_ON &&
	    dev && of_machine_is_compatible("innioasis,y2") &&
	    (of_device_is_compatible(dev->of_node, "cirrus,cs43131") ||
	     of_device_is_compatible(dev->of_node, "mediatek,mt6582-i2c")))
		y2_pm_note(step, ret);
}
#endif
