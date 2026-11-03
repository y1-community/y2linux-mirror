// SPDX-License-Identifier: GPL-2.0-only
/* MT6582 WACS2 transport: canonical owner shared by USB and MT6323 MFD.
 * Offsets/INIT/SYNC contract: retained MT6582 BSP pwrap_hal and our USBACM-03.
 * No reset, bus reinitialization, arbitration change or wrapper exception IRQ.
 */
#include "/project/kernel/diagnostic/pwrap.h"
#include "policy.h"
#include "shared.h"
#include "cpu-dvfs.h"
#include "pwrap-readiness-policy.h"
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

struct y2_wrap {
	void __iomem *base;
	struct regmap *map;
	struct device *dev;
};
static DEFINE_MUTEX(y2_wrap_lock);
static struct y2_wrap *y2_wrap;
static bool cpu_dvfs_prepared;
static bool cpu_idle_slots_prepared;
static unsigned cpu_selector_address;
static int cpu_voltage_error, cpu_voltage_first_error;
static const char *cpu_voltage_stage = "not_attempted", *cpu_voltage_first_stage = "none";
/* Raw readiness operands of the last admission/request, retained for diagnostics. */
static unsigned ready_mux, ready_wrap, ready_arb;
static const char *ready_reason = "not_read";
static bool wrap_dvfs_ready(struct y2_wrap *w)
{
	ready_mux = readl(w->base);
	ready_wrap = readl(w->base + 4);
	ready_arb = readl(w->base + 0x50);
	ready_reason = y2_pwrap_dvfs_unready(ready_mux, ready_wrap, ready_arb);
	if (!ready_reason) {
		ready_reason = "ready";
		return true;
	}
	return false;
}

static unsigned wrap_read(void *p, unsigned off) { return readl(p + off); }
static void wrap_write(void *p, unsigned off, unsigned val) { writel(val, p + off); }
static void wrap_delay(void *p) { udelay(10); }
static struct y2_pwrap_io wrap_io(struct y2_wrap *w)
{
	return (struct y2_pwrap_io){w->base, wrap_read, wrap_write, wrap_delay};
}

/* Regmap serializes all commands, including RMW. A fault poisons the channel:
 * subsequent requests refuse unowned pending state rather than clear it. */
static int wrap_reg_read(void *context, unsigned reg, unsigned *val)
{
	struct y2_wrap *w = context;
	struct y2_pwrap_io io = wrap_io(w);
	unsigned state = readl(w->base + 0xa0);
	int ret;
	if (reg & 1)
		return -EINVAL;
	ret = y2_pwrap_wait_sync(&io, &state);
	if (ret)
		return ret;
	writel((reg >> 1) << 16, w->base + 0x9c);
	ret = y2_pwrap_wait(&io, &state, 1);
	if (ret)
		return ret;
	*val = state & 0xffff;
	writel(1, w->base + 0xa4);
	return y2_pwrap_wait(&io, &state, 0);
}
static int wrap_reg_write(void *context, unsigned reg, unsigned val)
{
	struct y2_wrap *w = context;
	struct y2_pwrap_io io = wrap_io(w);
	unsigned state, old, mask = 0;
	int ret;
	/* Single MFD ownership; policy.h limits each child to its reviewed fields.
	 * Charger current/CV use bounded stock-derived levels / 4.175V. RTC and both watchdogs
	 * have separate owners.
	 * INT_CON/STATUS are upstream mask/W1C registers. Backlight changes duty,
	 * never sink current. No direct WACS VPROC or calibration writes. CPU
	 * voltage requests use only the separately guarded stock SPM slots. */
	mask = y2_pmic_write_mask(reg);
	if (!mask || !y2_pmic_value_allowed(reg, val))
		return -EPERM;
	if (mask != 0xffff) {
		ret = wrap_reg_read(w, reg, &old);
		if (ret)
			return ret;
		if ((old ^ val) & ~mask)
			return -EPERM;
	}
	state = readl(w->base + 0xa0);
	ret = y2_pwrap_wait_sync(&io, &state);
	if (ret)
		return ret;
	writel(BIT(31) | ((reg >> 1) << 16) | (val & 0xffff), w->base + 0x9c);
	return y2_pwrap_wait(&io, &state, 0);
}
static bool wrap_readable(struct device *dev, unsigned reg)
{
	return !(reg & 1) && (reg <= 0xffe || (reg >= 0x8000 && reg <= 0x803e));
}
static bool wrap_precious(struct device *dev, unsigned reg)
{
	return reg == 0x8002; /* RTC_IRQ_STA clears on read; exclude debugfs dumps. */
}
static const struct regmap_config wrap_config = {
    .reg_bits = 16,
    .val_bits = 16,
    .reg_stride = 2,
    .max_register = 0x803e,
    .readable_reg = wrap_readable,
    .precious_reg = wrap_precious,
    /* WACS uses bounded MMIO/udelay only. IRQ-safe serialization also permits
     * the upstream poweroff callback after interrupts have been disabled. */
    .fast_io = true,
    .reg_read = wrap_reg_read,
    .reg_write = wrap_reg_write,
    .cache_type = REGCACHE_NONE,
};
int y2_pmic_cpu_voltage_ready(void)
{
	unsigned control, selector;
	int ret = -EPROBE_DEFER;
	mutex_lock(&y2_wrap_lock);
	if (y2_wrap) {
		ret = regmap_read(y2_wrap->map, 0x216, &control);
		if (!ret) ret = regmap_read(y2_wrap->map, (control & BIT(1)) ? 0x220 : 0x21e, &selector);
		/* 700 mV + selector * 6.25 mV. All enabled OPPs require 1.15 V.
		 * All running operating points retain the stock 1.15-V requirement. */
		if (!ret && (selector & 0x7f) != 72) ret = -ERANGE;
	}
	mutex_unlock(&y2_wrap_lock);
	return ret;
}
static int y2_pmic_cpu_selector(struct y2_wrap *w)
{
	unsigned control, selector;
	int ret = regmap_read(w->map, 0x216, &control);
	if (!ret) ret = regmap_read(w->map, (control & BIT(1)) ? 0x220 : 0x21e, &selector);
	if (ret) return ret;
	selector &= 0x7f; /* Baseline read retains the established field mask. */
	return selector == 72 || selector == 80 || selector == 88 ? selector : -ERANGE;
}
int y2_pmic_cpu_voltage_get(void)
{
	int ret;
	mutex_lock(&y2_wrap_lock);
	ret = y2_wrap ? y2_pmic_cpu_selector(y2_wrap) : -EPROBE_DEFER;
	mutex_unlock(&y2_wrap_lock);
	return ret;
}
int y2_pmic_cpu_dvfs_prepare(void)
{
	static const unsigned selectors[] = {88, 80, 72};
	unsigned slot, reg, control, address, actual;
	int ret = -EPROBE_DEFER;
	mutex_lock(&y2_wrap_lock);
	smp_store_release(&cpu_idle_slots_prepared, false);
	if (!y2_wrap) goto out;
	cpu_voltage_stage = "active_selector";
	ret = y2_pmic_cpu_selector(y2_wrap);
	if (ret < 0) goto out;
	if (ret != 72) { ret = -EBUSY; goto out; }
	cpu_voltage_stage = "selector_mode";
	ret = regmap_read(y2_wrap->map, 0x216, &control);
	if (ret) goto out;
	address = (control & BIT(1)) ? 0x220 : 0x21e;
	if (cpu_dvfs_prepared && address != cpu_selector_address) { ret = -EIO; goto out; }
	ret = regmap_read(y2_wrap->map, address, &control);
	if (ret) goto out;
	/* Slot writes replace this complete word, unlike baseline field reads. */
	if (control & ~0x7fU) { ret = -EOPNOTSUPP; goto out; }
	cpu_voltage_stage = "pwrap_readiness";
	/* Require the inherited, fully enabled arbitration contract; never
	 * rewrite arbitration to make a DVFS request appear possible. */
	if (!wrap_dvfs_ready(y2_wrap)) { ret = -EOPNOTSUPP; goto out; }
	/* MT6323 VPROC_CON5 bit1 selects VOSEL_ON (220) versus software
	 * VOSEL (21e). Y2 physically boots in software mode. Route the same
	 * stock SPM/PWRAP protocol to the active bank; never flip selector mode
	 * or admit an inactive bank. Generic MFD writes remain prohibited. */
	cpu_voltage_stage = "baseline_active_feedback";
	ret = regmap_read(y2_wrap->map, 0x224, &actual); /* NI_VPROC_VOSEL */
	if (ret) goto out;
	if ((actual & 0x7f) != 72) { ret = -ERANGE; goto out; }
	cpu_voltage_stage = "pwrap_slots";
	for (slot = 0; slot < 3; slot++) {
		reg = 0xe4 + slot * 8;
		writel(address, y2_wrap->base + reg);
		writel(selectors[slot], y2_wrap->base + reg + 4);
		if (readl(y2_wrap->base + reg) != address ||
		    readl(y2_wrap->base + reg + 4) != selectors[slot]) {
			ret = -EIO; goto out;
		}
	}
	/* The retained DORMANT PCM requests slots 5/6 through SPM_AP_DVFS_CON.
	 * Full suspend also owns slot 7. Prepare them BEFORE admitting runtime
	 * DORMANT: inherited zero addresses would target charger register 0.
	 * All idle/sleep requests retain the validated 1.15-V CPU floor. */
	cpu_voltage_stage = "pwrap_idle_slots";
	for (slot = 5; slot < 8; slot++) {
		reg = 0xe4 + slot * 8;
		writel(address, y2_wrap->base + reg);
		writel(72, y2_wrap->base + reg + 4);
		if (readl(y2_wrap->base + reg) != address ||
		    readl(y2_wrap->base + reg + 4) != 72) {
			ret = -EIO; goto out;
		}
	}
	/* Baseline request proves the normal PCM handshake without raising VPROC. */
	cpu_voltage_stage = "baseline_spm_handshake_and_readback";
	ret = y2_spm_cpu_voltage_request(2);
	udelay(40);
	if (!ret && y2_pmic_cpu_selector(y2_wrap) != 72) ret = -EIO;
	if (!ret) ret = regmap_read(y2_wrap->map, 0x224, &actual);
	if (!ret && (actual & 0x7f) != 72) ret = -EIO;
	if (!ret) {
		cpu_selector_address = address;
		cpu_dvfs_prepared = true;
		smp_store_release(&cpu_idle_slots_prepared, true);
	}
out:
	cpu_voltage_error = ret;
	if (ret < 0 && ret != -EPROBE_DEFER && ret != -EBUSY && !cpu_voltage_first_error) {
		cpu_voltage_first_error = ret; cpu_voltage_first_stage = cpu_voltage_stage;
	}
	mutex_unlock(&y2_wrap_lock);
	if (ret == -EIO || ret == -ETIMEDOUT) y2_cpu_dvfs_fault();
	return ret;
}
int y2_pmic_idle_slots_ready(void)
{
	return smp_load_acquire(&cpu_idle_slots_prepared);
}
int y2_pmic_cpu_voltage_set(unsigned selector)
{
	unsigned slot, reg, control, actual;
	int ret = -EPROBE_DEFER;
	if (selector == 88) slot = 0;
	else if (selector == 80) slot = 1;
	else if (selector == 72) slot = 2;
	else return -EINVAL;
	mutex_lock(&y2_wrap_lock);
	if (!y2_wrap) goto out;
	if (!cpu_dvfs_prepared) { ret = -EACCES; goto out; }
	cpu_voltage_stage = "active_selector";
	ret = y2_pmic_cpu_selector(y2_wrap);
	if (ret < 0) goto out;
	cpu_voltage_stage = "selector_mode";
	ret = regmap_read(y2_wrap->map, 0x216, &control);
	if (ret) goto out;
	if (((control & BIT(1)) ? 0x220 : 0x21e) != cpu_selector_address) {
		ret = -EIO; goto out;
	}
	ret = regmap_read(y2_wrap->map, cpu_selector_address, &control);
	if (ret) goto out;
	if (control & ~0x7fU) { ret = -EOPNOTSUPP; goto out; }
	reg = 0xe4 + slot * 8;
	cpu_voltage_stage = "pwrap_readiness";
	if (!wrap_dvfs_ready(y2_wrap)) { ret = -EIO; goto out; }
	cpu_voltage_stage = "pwrap_slot_readback";
	if (readl(y2_wrap->base + reg) != cpu_selector_address ||
	    readl(y2_wrap->base + reg + 4) != selector) { ret = -EIO; goto out; }
	cpu_voltage_stage = "voltage_spm_handshake_and_readback";
	ret = y2_spm_cpu_voltage_request(slot);
	/* The source requires 40 us before any frequency increase. Waiting on
	 * decreases too is conservative and keeps readback after settling. */
	udelay(40);
	if (!ret && y2_pmic_cpu_selector(y2_wrap) != (int)selector) ret = -EIO;
	if (!ret) ret = regmap_read(y2_wrap->map, 0x224, &actual);
	if (!ret && (actual & 0x7f) != selector) ret = -EIO;
out:
	if (ret < 0) smp_store_release(&cpu_idle_slots_prepared, false);
	cpu_voltage_error = ret;
	if (ret < 0 && ret != -EPROBE_DEFER && ret != -EBUSY && !cpu_voltage_first_error) {
		cpu_voltage_first_error = ret; cpu_voltage_first_stage = cpu_voltage_stage;
	}
	mutex_unlock(&y2_wrap_lock);
	return ret;
}
static int cpu_voltage_get(char *buffer, const struct kernel_param *kp)
{
	int selector = y2_pmic_cpu_voltage_get();
	return scnprintf(buffer, PAGE_SIZE, "%d\n", selector < 0 ? selector :
		700000 + selector * 6250);
}
static const struct kernel_param_ops cpu_voltage_ops = { .get = cpu_voltage_get };
module_param_cb(cpu_voltage_uv, &cpu_voltage_ops, NULL, 0400);
static int cpu_voltage_state_get(char *buffer, const struct kernel_param *kp)
{
	unsigned mode = 0, selector = 0, actual = 0;
	int ret = -EPROBE_DEFER;
	mutex_lock(&y2_wrap_lock);
	if (y2_wrap) {
		ret = regmap_read(y2_wrap->map, 0x216, &mode);
		if (!ret) ret = regmap_read(y2_wrap->map, mode & 2 ? 0x220 : 0x21e, &selector);
		if (!ret) ret = regmap_read(y2_wrap->map, 0x224, &actual);
	}
	mutex_unlock(&y2_wrap_lock);
	return sysfs_emit(buffer, "owner=SPM_PWRAP mode=%s control=%#x address=%#x selector=%#x active_selector=%#x prepared=%u idle_slots_prepared=%u prepared_address=%#x last_error=%d read_error=%d stage=%s first_error=%d first_stage=%s\n",
		mode & 2 ? "hardware_VOSEL_ON" : "software_VOSEL", mode,
		mode & 2 ? 0x220 : 0x21e, selector, actual, cpu_dvfs_prepared,
		y2_pmic_idle_slots_ready(), cpu_selector_address, cpu_voltage_error, ret, cpu_voltage_stage, cpu_voltage_first_error, cpu_voltage_first_stage);
}
static const struct kernel_param_ops voltage_state_ops = { .get = cpu_voltage_state_get };
module_param_cb(cpu_voltage_state, &voltage_state_ops, NULL, 0400);
/* Read-only wrapper ownership: live MUX_SEL/WRAP_EN/HIPRIO_ARB_EN/WACS2
 * operands, the retained operands of the last admission and all DVFS slots.
 * MMIO reads only; the WACS2 FSM and PMIC are not touched. */
static int pwrap_readiness_get(char *buffer, const struct kernel_param *kp)
{
	unsigned mux = 0, wrap = 0, arb = 0, wacs2 = 0, init = 0, rdata = 0, slot[16] = {0}, i;
	const char *live = "no_wrapper";
	int n;
	mutex_lock(&y2_wrap_lock);
	if (y2_wrap) {
		mux = readl(y2_wrap->base);
		wrap = readl(y2_wrap->base + 4);
		arb = readl(y2_wrap->base + 0x50);
		wacs2 = readl(y2_wrap->base + 0x94);
		init = readl(y2_wrap->base + 0x98);
		rdata = readl(y2_wrap->base + 0xa0);
		for (i = 0; i < 16; i++)
			slot[i] = readl(y2_wrap->base + 0xe4 + i * 4);
		live = y2_pwrap_dvfs_unready(mux, wrap, arb);
		if (!live) live = "ready";
	}
	n = sysfs_emit(buffer, "mux=%#x wrap_en=%#x hiprio_arb_en=%#x implemented=%#x wacs2_en=%#x init_done2=%#x wacs2_rdata=%#x live=%s last_mux=%#x last_wrap_en=%#x last_arb=%#x last=%s",
		mux, wrap, arb, Y2_PWRAP_ARB_IMPLEMENTED, wacs2, init, rdata, live,
		ready_mux, ready_wrap, ready_arb, ready_reason);
	for (i = 0; i < 8; i++)
		n += sysfs_emit_at(buffer, n, " slot%u=%#x:%#x", i, slot[i * 2], slot[i * 2 + 1]);
	n += sysfs_emit_at(buffer, n, "\n");
	mutex_unlock(&y2_wrap_lock);
	return n;
}
static const struct kernel_param_ops pwrap_readiness_ops = { .get = pwrap_readiness_get };
module_param_cb(pwrap_readiness, &pwrap_readiness_ops, NULL, 0400);
int y2_pmic_spm_prepare(void)
{
	unsigned reg, value, control = 0, selector, address;
	int ret = -EPROBE_DEFER;
	mutex_lock(&y2_wrap_lock);
	if (!y2_wrap) goto out;
	ret = regmap_read(y2_wrap->map, 0, &value);
	if (!ret && (value & 0x18)) ret = -EBUSY;
	if (!ret) ret = regmap_read(y2_wrap->map, 0x01a, &value);
	if (!ret && (value & 0x10)) ret = -EBUSY;
	if (!ret) ret = regmap_read(y2_wrap->map, 0x216, &control);
	address = (control & BIT(1)) ? 0x220 : 0x21e;
	if (!ret) ret = regmap_read(y2_wrap->map, address, &selector);
	if (!ret && selector != 72) ret = -ERANGE;
	if (!ret) ret = regmap_read(y2_wrap->map, 0x224, &value);
	if (!ret && (value & 0x7f) != 72) ret = -ERANGE;
	if (ret) goto out;
	/* Stock SPM sleep slots 5/6/7 use the currently selected VPROC bank.
	 * Own them through the existing wrapper provider. Keep ALL three at
	 * 1.15 V: the 1.05-V stock sleep value is deliberately not adopted as
	 * an operating point or a prerequisite for this suspend implementation.
	 * No arbitration, PMIC selector ownership or normal DVFS slot changes. */
	for (reg = 0x10c; reg <= 0x11c; reg += 8) {
		writel(address, y2_wrap->base + reg);
		writel(0x48, y2_wrap->base + reg + 4);
		if (readl(y2_wrap->base + reg) != address || readl(y2_wrap->base + reg + 4) != 0x48) {
			smp_store_release(&cpu_idle_slots_prepared, false);
			ret = -EIO;
			break;
		}
	}
out:
	mutex_unlock(&y2_wrap_lock);
	return ret;
}
/* POWER-FINAL-13: stock VPROC sleep voltage for full system sleep only
 * (vproc-sleep-policy.h). Never blocks sleep: a refused or failed begin leaves
 * (or restores) the proven 1.15-V software configuration and sleep continues.
 * Called only from the platform ->enter() with IRQs off on the sole online
 * CPU, around the actual PCM sleep: cpufreq is suspended, cpuidle cannot run
 * and no other CPU exists, so no DVFS step can observe the HW-mode window.
 * regmap's own (spinlock) serialization covers each WACS2 transaction; the
 * sleeping y2_wrap_lock is deliberately not taken here. */
static struct y2_vproc_sleep vproc_sleep;
static bool vproc_sleep_enabled = true;
module_param_named(vproc_sleep, vproc_sleep_enabled, bool, 0600);
static unsigned vproc_sleep_entries, vproc_sleep_restores, vproc_sleep_failures;
static int vproc_sleep_error;
static const char *vproc_sleep_stage = "not_attempted";
static int vproc_sleep_restore_locked(struct regmap *map)
{
	unsigned n, i, value;
	int ret, first = 0;
	for (n = 0; n < Y2_VPROC_SLEEP_STEPS; n++) {
		const struct y2_vproc_field *f;
		i = y2_vproc_restore_index(n);
		f = &y2_vproc_sleep_fields[i];
		ret = regmap_update_bits(map, f->reg, f->mask, vproc_sleep.saved[i] & f->mask);
		if (!ret) ret = regmap_read(map, f->reg, &value);
		if (!ret && (value & f->mask) != (vproc_sleep.saved[i] & f->mask)) ret = -EIO;
		if (ret && !first) first = ret;
	}
	vproc_sleep.active = 0;
	return first;
}
/* POWER-FINAL-16 G5: read-only stock PMIC golden comparison at sleep entry
 * (pmic-golden-policy.h). Same context and regmap rules as the VPROC step. */
#include "pmic-golden-policy.h"
static unsigned short pmic_golden_entry[Y2_PMIC_GOLDEN_ROWS];
static unsigned pmic_golden_captures, pmic_golden_differences;
static int pmic_golden_error = -ENODATA;
void y2_pmic_golden_capture(void)
{
	unsigned i, value, differences = 0;
	int ret = 0;
	if (!y2_wrap) return;
	for (i = 0; i < Y2_PMIC_GOLDEN_ROWS && !ret; i++) {
		ret = regmap_read(y2_wrap->map, y2_pmic_golden[i].reg, &value);
		if (ret) break;
		pmic_golden_entry[i] = value;
		if ((value & y2_pmic_golden[i].mask) != y2_pmic_golden[i].golden) differences++;
	}
	pmic_golden_error = ret;
	if (!ret) { pmic_golden_captures++; pmic_golden_differences = differences; }
}
static int pmic_golden_get(char *buffer, const struct kernel_param *kp)
{
	unsigned i;
	int n = sysfs_emit(buffer, "source=Y2_FM_mt6323_power_gs_suspend_data rows=%zu captures=%u entry_differences=%u error=%d\n",
		Y2_PMIC_GOLDEN_ROWS, pmic_golden_captures, pmic_golden_differences, pmic_golden_error);
	for (i = 0; i < Y2_PMIC_GOLDEN_ROWS && pmic_golden_captures; i++)
		n += sysfs_emit_at(buffer, n, "%03x m=%x g=%x e=%x %u\n", y2_pmic_golden[i].reg,
			y2_pmic_golden[i].mask, y2_pmic_golden[i].golden, pmic_golden_entry[i],
			(pmic_golden_entry[i] & y2_pmic_golden[i].mask) == y2_pmic_golden[i].golden);
	return n;
}
static const struct kernel_param_ops pmic_golden_ops = { .get = pmic_golden_get };
module_param_cb(pmic_golden, &pmic_golden_ops, NULL, 0400);

void y2_pmic_vproc_sleep_begin(void)
{
	unsigned control, software, actual, value, i;
	int ret = 0;
	if (!y2_wrap || !READ_ONCE(vproc_sleep_enabled) || vproc_sleep.active) return;
	vproc_sleep_stage = "read";
	ret = regmap_read(y2_wrap->map, 0x216, &control);
	if (!ret) ret = regmap_read(y2_wrap->map, 0x21e, &software);
	if (!ret) ret = regmap_read(y2_wrap->map, 0x224, &actual); /* NI_VPROC_VOSEL */
	if (ret) goto fail;
	vproc_sleep_stage = "plan";
	if (y2_vproc_sleep_plan(&vproc_sleep, control, software, actual)) { ret = -ERANGE; goto fail; }
	for (i = 0; i < Y2_VPROC_SLEEP_STEPS; i++) {
		ret = regmap_read(y2_wrap->map, y2_vproc_sleep_fields[i].reg, &vproc_sleep.saved[i]);
		if (ret) goto fail;
		/* An unrestorable original would be refused by the write policy. */
		if (!y2_pmic_value_allowed(y2_vproc_sleep_fields[i].reg, vproc_sleep.saved[i])) {
			ret = -ERANGE; goto fail;
		}
	}
	vproc_sleep.active = 1;
	vproc_sleep_stage = "apply";
	for (i = 0; i < Y2_VPROC_SLEEP_STEPS; i++) {
		const struct y2_vproc_field *f = &y2_vproc_sleep_fields[i];
		ret = regmap_update_bits(y2_wrap->map, f->reg, f->mask, vproc_sleep.planned[i]);
		if (!ret) ret = regmap_read(y2_wrap->map, f->reg, &value);
		if (!ret && (value & f->mask) != vproc_sleep.planned[i]) ret = -EIO;
		if (ret) goto undo;
	}
	/* The mode switch itself must not move the live voltage. */
	vproc_sleep_stage = "verify";
	udelay(40);
	ret = regmap_read(y2_wrap->map, 0x224, &actual);
	if (!ret && (actual & 0x7f) != Y2_VPROC_ACTIVE_SELECTOR) ret = -EIO;
	if (ret) goto undo;
	vproc_sleep_entries++;
	vproc_sleep_stage = "armed";
	goto out;
undo:
	vproc_sleep_restore_locked(y2_wrap->map);
fail:
	vproc_sleep_failures++;
	vproc_sleep_error = ret;
out:
	return;
}
void y2_pmic_vproc_sleep_end(void)
{
	int ret;
	if (y2_wrap && vproc_sleep.active) {
		ret = vproc_sleep_restore_locked(y2_wrap->map);
		if (ret) {
			vproc_sleep_failures++;
			vproc_sleep_error = ret;
			vproc_sleep_stage = "restore_failed";
			pr_err("Y2VPROC: sleep configuration restore failed: %d\n", ret);
		} else {
			vproc_sleep_restores++;
			vproc_sleep_stage = "restored";
		}
	}
}
/* POWER-FINAL-18: stock PMIC suspend fields (pmic-sleep-policy.h), same
 * context and regmap rules as the VPROC step. `pmic_sleep` is a group bitmask
 * armed by userspace (power_features); 0 keeps 17's behaviour. */
#include "pmic-sleep-policy.h"
static unsigned pmic_sleep_groups;
module_param_named(pmic_sleep, pmic_sleep_groups, uint, 0644);
static struct y2_ps_state pmic_sleep;
static unsigned pmic_sleep_entries, pmic_sleep_restores, pmic_sleep_failures, pmic_sleep_last_changed;
static unsigned pmic_sleep_vsys_refusals;
static int pmic_sleep_error;
static int pmic_sleep_read(void *context, unsigned reg, unsigned *value)
{
	return regmap_read(context, reg, value);
}
static int pmic_sleep_update(void *context, unsigned reg, unsigned mask, unsigned value)
{
	return regmap_update_bits(context, reg, mask, value);
}
void y2_pmic_sleep_begin(void)
{
	unsigned groups = READ_ONCE(pmic_sleep_groups) & Y2_PS_ALL;
	struct y2_ps_io io;
	int ret;
	if (!y2_wrap || !groups) return;
	if (groups & Y2_PS_VSYS) {
		/* 27: hardware mode must select the voltage VSYS already has. */
		unsigned control, vosel, on, sleep, actual;
		if (regmap_read(y2_wrap->map, 0x23c, &control) ||
		    regmap_read(y2_wrap->map, 0x244, &vosel) ||
		    regmap_read(y2_wrap->map, 0x246, &on) ||
		    regmap_read(y2_wrap->map, 0x248, &sleep) ||
		    regmap_read(y2_wrap->map, 0x24a, &actual) ||
		    !y2_ps_vsys_ready(control, vosel, on, sleep, actual)) {
			groups &= ~Y2_PS_VSYS;
			pmic_sleep_vsys_refusals++;
		}
	}
	io = (struct y2_ps_io){ y2_wrap->map, pmic_sleep_read, pmic_sleep_update };
	ret = y2_ps_apply(&io, groups, &pmic_sleep);
	if (ret) {
		pmic_sleep_failures++;
		pmic_sleep_error = ret;
		return;
	}
	pmic_sleep_entries++;
	pmic_sleep_last_changed = pmic_sleep.changed;
}
void y2_pmic_sleep_end(void)
{
	struct y2_ps_io io;
	int ret;
	if (!y2_wrap || !pmic_sleep.changed) return;
	io = (struct y2_ps_io){ y2_wrap->map, pmic_sleep_read, pmic_sleep_update };
	ret = y2_ps_restore(&io, &pmic_sleep);
	if (ret) {
		pmic_sleep_failures++;
		pmic_sleep_error = ret;
		pr_err("Y2PMIC: sleep field restore failed: %d changed=%#x\n", ret, pmic_sleep.changed);
	} else {
		pmic_sleep_restores++;
	}
}
static int pmic_sleep_state_get(char *buffer, const struct kernel_param *kp)
{
	return sysfs_emit(buffer, "groups=%#x entries=%u restores=%u failures=%u last_error=%d last_changed=%#x pending=%#x vsys_refusals=%u\n",
		READ_ONCE(pmic_sleep_groups), pmic_sleep_entries, pmic_sleep_restores,
		pmic_sleep_failures, pmic_sleep_error, pmic_sleep_last_changed, pmic_sleep.changed,
		pmic_sleep_vsys_refusals);
}
static const struct kernel_param_ops pmic_sleep_state_ops = { .get = pmic_sleep_state_get };
module_param_cb(pmic_sleep_state, &pmic_sleep_state_ops, NULL, 0400);

static int vproc_sleep_state_get(char *buffer, const struct kernel_param *kp)
{
	return sysfs_emit(buffer, "enabled=%u active=%d stage=%s entries=%u restores=%u failures=%u last_error=%d sleep_uv=%u\n",
		READ_ONCE(vproc_sleep_enabled), vproc_sleep.active, vproc_sleep_stage,
		vproc_sleep_entries, vproc_sleep_restores, vproc_sleep_failures,
		vproc_sleep_error, 700000 + Y2_VPROC_SLEEP_SELECTOR * 6250);
}
static const struct kernel_param_ops vproc_sleep_state_ops = { .get = vproc_sleep_state_get };
module_param_cb(vproc_sleep_state, &vproc_sleep_state_ops, NULL, 0400);
int y2_pmic_snapshot(struct y2_pwrap_snapshot *s)
{
	int ret = -EPROBE_DEFER;
	*s = (struct y2_pwrap_snapshot){.magic = Y2_PWRAP_MAGIC, .result = ret};
	mutex_lock(&y2_wrap_lock);
	if (y2_wrap) {
		struct y2_wrap *w = y2_wrap;
		s->mux = readl(w->base);
		s->wrap = readl(w->base + 4);
		s->arb = readl(w->base + 0x50);
		s->channel = readl(w->base + 0x94);
		s->init = readl(w->base + 0x98);
		s->before = readl(w->base + 0xa0);
		ret = regmap_read(w->map, 0x100, &s->cid);
		if (!ret) {
			s->valid = 1;
			ret = regmap_read(w->map, 0x502, &s->vusb);
		}
		if (!ret) {
			s->valid |= 2;
			ret = regmap_read(w->map, 0, &s->chrdet);
		}
		if (!ret)
			s->valid |= 4;
		s->after = readl(w->base + 0xa0);
		s->result = ret;
	}
	mutex_unlock(&y2_wrap_lock);
	return ret;
}
static int wrap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct y2_wrap *w;
	struct y2_pwrap_snapshot s;
	struct y2_pwrap_io io;
	struct clk *clk;
	int ret;
	w = devm_kzalloc(dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "wrap clock\n");
	w->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(w->base))
		return PTR_ERR(w->base);
	w->dev = dev;
	io = wrap_io(w);
	y2_pwrap_probe(&io, &s);
	if (s.result)
		return dev_err_probe(dev, s.result, "inherited WACS2 not ready\n");
	w->map = devm_regmap_init(dev, NULL, w, &wrap_config);
	if (IS_ERR(w->map))
		return PTR_ERR(w->map);
	mutex_lock(&y2_wrap_lock);
	if (y2_wrap) {
		mutex_unlock(&y2_wrap_lock);
		return -EBUSY;
	}
	y2_wrap = w;
	mutex_unlock(&y2_wrap_lock);
	/* Built-in non-unbindable owner: USB uses this map throughout the window. */
	platform_set_drvdata(pdev, w);
	ret = devm_of_platform_populate(dev);
	if (ret) {
		mutex_lock(&y2_wrap_lock);
		y2_wrap = NULL;
		mutex_unlock(&y2_wrap_lock);
		return dev_err_probe(dev, ret, "PMIC children\n");
	}
	dev_info(dev,
		 "MT6323 CID=%04x VUSB=%04x CHRDET=%u; serialized MFD regmap; scoped power writes\n",
		 s.cid, s.vusb, !!(s.chrdet & 32));
	return 0;
}
static const struct of_device_id wrap_match[] = {{.compatible = "innioasis,y2-pwrap"}, {}};
static struct platform_driver wrap_driver = {
    .probe = wrap_probe,
    .driver = {.name = "y2-pwrap", .of_match_table = wrap_match, .suppress_bind_attrs = true}};
static int __init wrap_init(void) { return platform_driver_register(&wrap_driver); }
subsys_initcall(wrap_init);
MODULE_LICENSE("GPL");
