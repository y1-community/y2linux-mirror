// SPDX-License-Identifier: GPL-2.0-only
/* INFRASYS/DDRPHY power-down companion of the vendor pcm_suspend (see
 * infra-pdn-policy.h for the hardware background).
 *
 *   y2_infra_pdn_wanted()   should the next full suspend remove INFRA power?
 *   y2_infra_pdn_save()     snapshot PERICFG (and read-only INFRACFG), before the PCM is armed
 *   y2_infra_pdn_restore()  write back what the power-down reset, right after the CPU returns
 *
 * Safety:
 *  - Any failed or unverifiable restore permanently falls back to the proven
 *    INFRA-retained mode for the rest of the boot.
 *  - The journal stage Y2_PM_INFRA_PDN_ENTRY is the last mark before WFI. If the
 *    previous boot ended on it, that boot died inside a power-down attempt, so
 *    this boot starts in retained mode and never repeats the attempt on its own.
 *  - /sys/firmware/y2_infra/mode ("powerdown" | "retained") is the owner's
 *    explicit switch; writing "powerdown" clears a fallback.
 */
#include <asm/barrier.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include "infra-pdn.h"
#include "infra-pdn-policy.h"
#include "pm-journal.h"

#define Y2_PERICFG_PHYS 0x10003000
#define Y2_PERICFG_SIZE 0x500
#define Y2_INFRACFG_PHYS 0x10201000
#define Y2_INFRACFG_SIZE 0x20
/* INFRACFG is only observed: TOPAXI_SI0_STA, TOPAXI_SI1_CTL, TOPAXI_MI_STA. */
static const unsigned short infracfg_offsets[] = { 0x000, 0x004, 0x01c };
#define N_INFRACFG ARRAY_SIZE(infracfg_offsets)
#define PDN0_INDEX 2	/* index of the PDN0 entry in y2_pericfg_regs */

static void __iomem *pericfg, *infracfg;
static struct y2_spm_io peri_io;
static DEFINE_MUTEX(mode_lock);
static bool powerdown_mode = true, fallback, ready, saved_valid;
static const char *fallback_reason = "none";
static unsigned reg_saved[Y2_INFRA_MAX_REGS], reg_woke[Y2_INFRA_MAX_REGS];
static unsigned fabric_saved[N_INFRACFG], fabric_woke[N_INFRACFG];
static unsigned saves, restores, restore_failures, last_lost, last_failed, total_lost, fabric_lost;
static int last_result;

static unsigned peri_read(void *context, unsigned reg)
{
	return readl(context + reg);
}
static void peri_write(void *context, unsigned reg, unsigned value)
{
	writel(value, context + reg);
	dsb(sy);
}
static void peri_delay(unsigned us)
{
	udelay(us);
}

bool y2_infra_pdn_wanted(void)
{
	return READ_ONCE(ready) && READ_ONCE(powerdown_mode) && !READ_ONCE(fallback);
}
/* Called with interrupts off, before the PCM is armed. */
int y2_infra_pdn_save(void)
{
	unsigned i;
	if (!y2_infra_pdn_wanted() || !pericfg)
		return -ENODEV;
	y2_infra_save(&peri_io, y2_pericfg_regs, Y2_PERICFG_REGS, reg_saved);
	for (i = 0; i < N_INFRACFG; i++)
		fabric_saved[i] = infracfg ? readl(infracfg + infracfg_offsets[i]) : 0;
	saved_valid = true;
	saves++;
	return 0;
}
static void record_fallback(const char *reason)
{
	WRITE_ONCE(fallback, true);
	fallback_reason = reason;
}
/* Called with interrupts off, immediately after cpu_suspend() returns, before
 * anything else touches a peripheral. Needs a preceding successful
 * y2_infra_pdn_save(). Only PERICFG is written; INFRACFG is read. */
int y2_infra_pdn_restore(void)
{
	struct y2_infra_result result;
	unsigned i, lost = 0;
	int ret;
	if (!saved_valid)
		return -ENODEV;
	saved_valid = false;
	for (i = 0; i < N_INFRACFG; i++) {
		fabric_woke[i] = infracfg ? readl(infracfg + infracfg_offsets[i]) : 0;
		if (fabric_woke[i] != fabric_saved[i])
			lost |= BIT(i);
	}
	fabric_lost = lost;
	ret = y2_infra_restore(&peri_io, y2_pericfg_regs, Y2_PERICFG_REGS, reg_saved,
			       reg_woke, &result);
	restores++;
	last_lost = result.lost;
	last_failed = result.failed;
	total_lost += hweight32(result.lost);
	last_result = ret;
	if (ret) {
		restore_failures++;
		record_fallback("restore_verify_failed");
	}
	y2_pm_note(ret ? "infra_failed" : result.lost ? "infra_restored" : "infra_kept", ret);
	/* Bring-up evidence: the first power-downs and every failure, then quiet. */
	if (ret || restores <= 8)
		pr_info("Y2INFRA: restore result=%d lost=%#x failed=%#x pdn0 %#x->%#x infracfg_changed=%#x\n",
			ret, result.lost, result.failed, reg_saved[PDN0_INDEX], reg_woke[PDN0_INDEX], lost);
	return ret;
}

static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", READ_ONCE(powerdown_mode) ? "powerdown" : "retained");
}
static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	bool power;
	if (sysfs_streq(buf, "powerdown"))
		power = true;
	else if (sysfs_streq(buf, "retained"))
		power = false;
	else
		return -EINVAL;
	mutex_lock(&mode_lock);
	WRITE_ONCE(powerdown_mode, power);
	if (power) {	/* an explicit owner decision clears a fallback */
		WRITE_ONCE(fallback, false);
		fallback_reason = "none";
	}
	mutex_unlock(&mode_lock);
	return count;
}
static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf,
		"mode=%s fallback=%u reason=%s ready=%u saves=%u restores=%u restore_failures=%u "
		"last_result=%d last_lost=%#x last_failed=%#x total_lost_regs=%u "
		"pdn0_saved=%#x pdn0_woke=%#x infracfg_changed=%#x regs=%u\n",
		READ_ONCE(powerdown_mode) ? "powerdown" : "retained", READ_ONCE(fallback), fallback_reason,
		READ_ONCE(ready), saves, restores, restore_failures, last_result, last_lost, last_failed,
		total_lost, reg_saved[PDN0_INDEX], reg_woke[PDN0_INDEX], fabric_lost, Y2_PERICFG_REGS);
}
/* What the most recent power-down reset, register by register. */
static ssize_t diff_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ssize_t n = 0;
	unsigned i;
	for (i = 0; i < Y2_PERICFG_REGS; i++)
		n += sysfs_emit_at(buf, n, "pericfg+%#05x saved=%#010x woke=%#010x%s\n",
			y2_pericfg_regs[i].offset, reg_saved[i], reg_woke[i],
			(last_lost & BIT(i)) ? (last_failed & BIT(i) ? " LOST-NOT-RESTORED" : " lost-restored") : "");
	for (i = 0; i < N_INFRACFG; i++)
		n += sysfs_emit_at(buf, n, "infracfg+%#05x saved=%#010x woke=%#010x%s\n",
			infracfg_offsets[i], fabric_saved[i], fabric_woke[i],
			(fabric_lost & BIT(i)) ? " changed" : "");
	return n;
}
static struct kobj_attribute mode_attr = __ATTR(mode, 0600, mode_show, mode_store);
static struct kobj_attribute state_attr = __ATTR_RO(state);
static struct kobj_attribute diff_attr = __ATTR_RO(diff);
static struct attribute *infra_attrs[] = { &mode_attr.attr, &state_attr.attr, &diff_attr.attr, NULL };
static const struct attribute_group infra_group = { .attrs = infra_attrs };

static int __init y2_infra_pdn_init(void)
{
	struct kobject *kobj;
	if (!IS_ENABLED(CONFIG_Y2_POWER))
		return 0;
	pericfg = ioremap(Y2_PERICFG_PHYS, Y2_PERICFG_SIZE);
	if (!pericfg) {
		record_fallback("pericfg_unmapped");
		return -ENOMEM;
	}
	peri_io = (struct y2_spm_io){ .context = pericfg, .read = peri_read, .write = peri_write,
				      .delay = peri_delay };
	infracfg = ioremap(Y2_INFRACFG_PHYS, Y2_INFRACFG_SIZE);
	if (!y2_pm_journal_ready()) {
		/* No retained breadcrumb means no way to prove an attempt survived. */
		record_fallback("journal_unavailable");
	} else if (y2_pm_previous_stage_was(Y2_PM_INFRA_PDN_ENTRY)) {
		record_fallback("previous_boot_died_in_infra_pdn");
		pr_warn("Y2INFRA: previous boot ended inside an INFRA power-down; retained mode\n");
	}
	WRITE_ONCE(ready, true);
	kobj = kobject_create_and_add("y2_infra", firmware_kobj);
	if (kobj && sysfs_create_group(kobj, &infra_group))
		pr_warn("Y2INFRA: sysfs unavailable\n");
	pr_info("Y2INFRA: mode=%s fallback=%u reason=%s\n",
		powerdown_mode ? "powerdown" : "retained", fallback, fallback_reason);
	return 0;
}
late_initcall(y2_infra_pdn_init);
