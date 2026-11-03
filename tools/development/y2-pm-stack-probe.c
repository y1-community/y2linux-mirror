// SPDX-License-Identifier: GPL-2.0-only
/* Optional, matching-kernel diagnostic after a reproduced suspend failure.
 * No hardware writes, freezer bypass or PM callback replacement. A bounded
 * delayed work item prints the suspender and blocked tasks into the existing
 * printk/ramoops console if PM_POST_SUSPEND has not arrived. No normal-idle
 * timer. An external-module taint remains until reboot: never call this a
 * clean product qualification, and archive evidence before removing it.
 */
#include <linux/module.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/workqueue.h>

static struct task_struct *suspender;
static struct delayed_work sample_work;
static unsigned int rounds;
static bool suppress_nested;
module_param(suppress_nested, bool, 0400);
MODULE_PARM_DESC(suppress_nested, "04-only diagnostic: suppress duplicate CPU PM save/restore pairs");
static unsigned cpu_depth, cluster_depth;
static void __iomem *gic;

static void irq_snapshot(const char *where)
{
	pr_err("Y2_PM_STACKS: %s GIC_ENABLE=%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
	       where, readl(gic + 0x100), readl(gic + 0x104), readl(gic + 0x108),
	       readl(gic + 0x10c), readl(gic + 0x110), readl(gic + 0x114), readl(gic + 0x118));
}

static int boundary(unsigned *depth, bool enter, bool repair)
{
	if (enter) {
		if (*depth >= 2)
			return NOTIFY_BAD;
		++*depth;
		return repair && *depth == 2 ? NOTIFY_STOP : NOTIFY_OK;
	}
	if (!*depth)
		return NOTIFY_BAD;
	--*depth;
	return repair && *depth ? NOTIFY_STOP : NOTIFY_OK;
}

/* Core syscore PM is already the full-suspend CPU/GIC context owner. The 04
 * platform path incorrectly enters it again after CIRQ masked device IRQs.
 * This opt-in diagnostic leaves the first save and final restore untouched;
 * only the nested pair is suppressed. Not used outside an explicit PM trial.
 * It does not program any hardware. Production fixes the call site instead.
 * At INT_MAX this is the first robust notifier: STOP at the duplicate ENTER
 * unwinds zero earlier callbacks and notifier_to_errno(NOTIFY_STOP) is zero. */
static int context_boundary(struct notifier_block *notifier, unsigned long event, void *unused)
{
	unsigned *depth;
	bool enter;
	int result;
	if (!READ_ONCE(suspender))
		return NOTIFY_OK;
	switch (event) {
	case CPU_PM_ENTER: depth = &cpu_depth; enter = true; break;
	case CPU_CLUSTER_PM_ENTER: depth = &cluster_depth; enter = true; break;
	case CPU_PM_EXIT:
	case CPU_PM_ENTER_FAILED: depth = &cpu_depth; enter = false; break;
	case CPU_CLUSTER_PM_EXIT:
	case CPU_CLUSTER_PM_ENTER_FAILED: depth = &cluster_depth; enter = false; break;
	default: return NOTIFY_OK;
	}
	irq_snapshot(enter ? "before_context_save" : "before_context_restore");
	result = boundary(depth, enter, suppress_nested);
	pr_err("Y2_PM_STACKS: boundary event=%lu cpu_depth=%u cluster_depth=%u result=%x repair=%u\n",
	       event, cpu_depth, cluster_depth, result, suppress_nested);
	return result;
}
static struct notifier_block boundary_notifier = { .notifier_call = context_boundary, .priority = INT_MAX };

/* Read-only banked timer evidence. No timer programming, MMIO or callback
 * replacement. This only runs inside the explicitly requested PM trial. */
static void timer_snapshot(const char *where)
{
	u32 lo, hi, clo, chi, control, frequency;
	asm volatile("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
	asm volatile("mrrc p15, 2, %0, %1, c14" : "=r" (clo), "=r" (chi));
	asm volatile("mrc p15, 0, %0, c14, c2, 1" : "=r" (control));
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (frequency));
	pr_err("Y2_PM_STACKS: %s cpu=%u jiffies=%lu count=%08x%08x compare=%08x%08x control=%x frequency=%u\n",
	       where, raw_smp_processor_id(), jiffies, hi, lo, chi, clo, control, frequency);
}

static int cpu_event(struct notifier_block *notifier, unsigned long event, void *unused)
{
	if (READ_ONCE(suspender)) {
		if (event == CPU_PM_ENTER)
			timer_snapshot("cpu_enter");
		else if (event == CPU_PM_EXIT || event == CPU_PM_ENTER_FAILED)
			timer_snapshot("cpu_return");
	}
	return NOTIFY_OK;
}
static struct notifier_block cpu_notifier = { .notifier_call = cpu_event, .priority = -100 };

static void syscore_return(void)
{
	if (READ_ONCE(suspender)) {
		timer_snapshot("syscore_return");
		irq_snapshot("after_core_restore");
		/* Separate a missing timer tick from an ordinary blocked task: the
		 * console snapshot precedes the one-second scheduled stack dump. */
		mod_delayed_work(system_highpri_wq, &sample_work, HZ);
	}
}
static struct syscore_ops syscore_probe = { .resume = syscore_return };

static void sample(struct work_struct *work)
{
	struct task_struct *group, *task, *blocked[16];
	unsigned int count = 0, i;
	pr_err("Y2_PM_STACKS: round=%u suspender=%d\n", ++rounds,
		suspender ? task_pid_nr(suspender) : -1);
	timer_snapshot("stack_sample");
	if (suspender)
		sched_show_task(suspender);
	rcu_read_lock();
	for_each_process_thread(group, task) {
		if (task == suspender || !(READ_ONCE(task->__state) & TASK_UNINTERRUPTIBLE))
			continue;
		get_task_struct(task);
		blocked[count++] = task;
		if (count == ARRAY_SIZE(blocked))
			goto collected;
	}
collected:
	rcu_read_unlock();
	for (i = 0; i < count; i++) {
		sched_show_task(blocked[i]);
		put_task_struct(blocked[i]);
	}
	pr_err("Y2_PM_STACKS: blocked_printed=%u end\n", count);
	if (rounds < 2)
		schedule_delayed_work(&sample_work, 10 * HZ);
}

static void stop(void)
{
	cancel_delayed_work_sync(&sample_work);
	if (suspender) {
		put_task_struct(suspender);
		suspender = NULL;
	}
}

static int pm_event(struct notifier_block *notifier, unsigned long event, void *unused)
{
	if (event == PM_SUSPEND_PREPARE) {
		stop();
		cpu_depth = cluster_depth = 0;
		get_task_struct(current);
		suspender = current;
		rounds = 0;
		schedule_delayed_work(&sample_work, 5 * HZ);
	} else if (event == PM_POST_SUSPEND) {
		pr_err("Y2_PM_STACKS: PM_POST cpu_depth=%u cluster_depth=%u\n", cpu_depth, cluster_depth);
		stop();
	}
	return NOTIFY_OK;
}
static struct notifier_block pm_notifier = { .notifier_call = pm_event };

static int __init probe_init(void)
{
	int ret;
	if (!of_machine_is_compatible("innioasis,y2"))
		return -ENODEV;
	/* Read-only alias of the existing Y2 GIC owner, no reservation/write. */
	gic = ioremap(0x10211000, 0x1000);
	if (!gic)
		return -ENOMEM;
	INIT_DELAYED_WORK(&sample_work, sample);
	ret = cpu_pm_register_notifier(&boundary_notifier);
	if (ret)
		goto unmap;
	ret = cpu_pm_register_notifier(&cpu_notifier);
	if (ret)
		goto unregister_boundary;
	ret = register_pm_notifier(&pm_notifier);
	if (ret) {
		cpu_pm_unregister_notifier(&cpu_notifier);
		goto unregister_boundary;
	}
	register_syscore_ops(&syscore_probe);
	return 0;
unregister_boundary:
	cpu_pm_unregister_notifier(&boundary_notifier);
unmap:
	iounmap(gic);
	return ret;
}
static void __exit probe_exit(void)
{
	unregister_syscore_ops(&syscore_probe);
	unregister_pm_notifier(&pm_notifier);
	cpu_pm_unregister_notifier(&cpu_notifier);
	cpu_pm_unregister_notifier(&boundary_notifier);
	stop();
	iounmap(gic);
}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded Y2 suspend blocked-task stack capture; diagnostic only");
