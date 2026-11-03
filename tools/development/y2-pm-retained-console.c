// SPDX-License-Identifier: GPL-2.0-only
/* Companion for an already loaded stack probe on kernels without module
 * unloading. No hardware access. A stalled device-phase stack must reach
 * ramoops while the UART/display consoles retain their normal PM behavior. */
#include <linux/module.h>
#include <linux/console.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>

static void retain(struct work_struct *work)
{
	struct console *con;
	console_list_lock();
	for_each_console(con) {
		if (!strcmp(con->name, "ramoops") && (con->flags & CON_ENABLED))
			console_srcu_write_flags(con, con->flags & ~CON_SUSPENDED);
	}
	console_list_unlock();
	pr_err("Y2_PM_RETAIN: RAM console retained for stalled PM stage; UART policy unchanged\n");
}
static DECLARE_DELAYED_WORK(retain_work, retain);
static int event(struct notifier_block *nb, unsigned long action, void *unused)
{
	if (action == PM_SUSPEND_PREPARE)
		mod_delayed_work(system_highpri_wq, &retain_work, 4 * HZ);
	else if (action == PM_POST_SUSPEND)
		cancel_delayed_work_sync(&retain_work);
	return NOTIFY_OK;
}
static struct notifier_block notifier = { .notifier_call = event };
static int __init retain_init(void) { return register_pm_notifier(&notifier); }
static void __exit retain_exit(void)
{
	unregister_pm_notifier(&notifier);
	cancel_delayed_work_sync(&retain_work);
}
module_init(retain_init);
module_exit(retain_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded retained-console companion for the Y2 PM stack diagnostic");
