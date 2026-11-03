"""Device-phase stall diagnostics (DEEPSLEEP-FINAL-04 source). Synthetic evidence,
never a device receipt: it proves the new retained records and sampler are wired
and behave, not that they localize the physical failure."""
from pathlib import Path
import json
import unittest
from test_audio import function
from test_power import run_c
from test_cpu_fix02 import overlay
ROOT = Path(__file__).resolve().parents[1]
JOURNAL = ROOT/'kernel/platform/pm-journal.c'


class DeviceSuspendTrail(unittest.TestCase):
    """exact03 failures end after a returned callback with no outstanding entry.
    The silent steps of device_suspend() must now leave their own records."""

    def setUp(self):
        self.main = overlay('drivers/base/power/main.c')
        self.suspend = function(self.main, 'device_suspend')

    def test_wait_precedes_every_silent_wait_and_lock_brackets_device_lock(self):
        s = self.suspend
        order = ['y2_pm_device(dev, Y2_PM_PHASE_WAIT, 0, false);', 'dpm_wait_for_subordinate(dev, async);',
                 'pm_runtime_barrier(dev);', 'y2_pm_device(dev, Y2_PM_PHASE_LOCK, 0, false);',
                 'device_lock(dev);', 'y2_pm_device(dev, Y2_PM_PHASE_LOCK, 0, true);',
                 'error = dpm_run_callback(callback, dev, state, info);']
        positions = [s.index(x) for x in order]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(s.count('Y2_PM_PHASE_WAIT'), 1)
        self.assertEqual(s.count('Y2_PM_PHASE_LOCK'), 2)

    def test_legacy_bus_suspend_is_logged_like_a_pm_callback(self):
        s = self.suspend
        enter = s.index('y2_pm_device(dev, Y2_PM_PHASE_SUSPEND, 0, false);')
        call = s.index('error = legacy_suspend(dev, state, dev->bus->suspend,')
        leave = s.index('y2_pm_device(dev, Y2_PM_PHASE_SUSPEND, error, true);')
        self.assertLess(enter, call)
        self.assertLess(call, leave)

    def test_new_phases_are_appended_and_decoded(self):
        header = (ROOT/'kernel/platform/pm-journal.h').read_text()
        phases = header[header.index('enum y2_pm_phase'):header.index('struct device;')]
        names = [x.strip().rstrip(',') for x in phases.splitlines() if x.strip().startswith('Y2_PM_PHASE_')]
        # Existing phase numbers (1..8) are unchanged; the new ones follow.
        self.assertEqual(names[0], 'Y2_PM_PHASE_PREPARE = 1')
        self.assertEqual(names[1:], ['Y2_PM_PHASE_SUSPEND', 'Y2_PM_PHASE_LATE', 'Y2_PM_PHASE_NOIRQ',
                                     'Y2_PM_PHASE_RESUME_NOIRQ', 'Y2_PM_PHASE_EARLY', 'Y2_PM_PHASE_RESUME',
                                     'Y2_PM_PHASE_COMPLETE', 'Y2_PM_PHASE_WAIT', 'Y2_PM_PHASE_LOCK'])

    def test_manifest_hash_tracks_the_regenerated_overlay(self):
        # overlay() verifies base and result SHA256; reaching here proves both.
        spec = next(x for x in json.loads((ROOT/'kernel/patches/manifest.json').read_text())['overlays']
                    if x['path'] == 'drivers/base/power/main.c')
        self.assertEqual(spec['patch'], '0059-pm-balance-prepare-refusal.patch')
        self.assertIn('Y2_PM_PHASE_WAIT', self.main)


class DeviceStallSampler(unittest.TestCase):
    def setUp(self):
        self.source = JOURNAL.read_text()

    def test_sampler_is_armed_only_by_a_successful_prepare_and_disarmed_by_the_next_mark(self):
        region = self.source[self.source.index('static struct delayed_work dpm_stall_work;'):
                             self.source.index('static void dpm_stall_console(void)')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
typedef int atomic_t;
#define ATOMIC_INIT(x) (x)
static int atomic_read(atomic_t *a) { return *a; }
static void atomic_set(atomic_t *a, int v) { *a = v; }
static int atomic_xchg(atomic_t *a, int v) { int o = *a; *a = v; return o; }
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define HZ 250
struct delayed_work { int pending; };
struct task { pid_t pid; } me = { 77 };
#define current (&me)
static int wq_object, queued, cancelled;
#define system_wq (&wq_object)
static unsigned long delay;
static void mod_delayed_work(int *wq, struct delayed_work *w, unsigned long d) { (void)wq; w->pending = 1; queued++; delay = d; }
static void cancel_delayed_work(struct delayed_work *w) { w->pending = 0; cancelled++; }
static int console_printk[4] = { 3 };
#define console_loglevel (console_printk[0])
enum { Y2_PM_DEVICES_SUSPENDED = 3, Y2_PM_DPM_PREPARED = 28 };
''' + region + function(self.source, 'dpm_stall_mark') + r'''
int main(void) {
 /* A refused prepare never arms the sampler. */
 dpm_stall_mark(Y2_PM_DPM_PREPARED, -16);
 assert(!queued && !atomic_read(&dpm_stall_armed));
 dpm_stall_mark(Y2_PM_DEVICES_SUSPENDED, 0);
 assert(!cancelled);
 /* Successful prepare arms the first round 4 s out, for the suspender. */
 dpm_stall_mark(Y2_PM_DPM_PREPARED, 0);
 assert(queued == 1 && delay == 4 * HZ && atomic_read(&dpm_stall_armed) && dpm_suspender == 77);
 /* A report that raised the console level is undone when the phase ends. */
 dpm_saved_loglevel = 3; dpm_loglevel_saved = true; console_loglevel = 10;
 dpm_stall_mark(Y2_PM_DEVICES_SUSPENDED, 0);
 assert(cancelled == 1 && !atomic_read(&dpm_stall_armed));
 assert(console_loglevel == 3 && !dpm_loglevel_saved);
 /* Normal completion leaves nothing to cancel on later marks. */
 dpm_stall_mark(Y2_PM_DEVICES_SUSPENDED, 0);
 assert(cancelled == 1);
 /* Re-arming starts a fresh round counter. */
 dpm_stall_round = 2;
 dpm_stall_mark(Y2_PM_DPM_PREPARED, 0);
 assert(dpm_stall_round == 0 && queued == 2);
 return 0;
}
''')

    def test_sampler_reports_before_the_backstop_and_records_both_ring_and_console(self):
        sample = function(self.source, 'dpm_stall_sample')
        mark = function(self.source, 'dpm_stall_mark')
        # First round 4 s after DPM_PREPARED, second 8 s later; backstop is 30 s.
        self.assertIn('4 * HZ', mark)
        self.assertIn('8 * HZ', sample)
        self.assertIn('dpm_stall_round < 2', sample)
        # Ring record first (survives a hard stop), then the rich console report.
        order = ['y2_pm_note("dpm_stall"', 'y2_pm_note(name, (int)get_wchan(s));', 'y2_pm_note("dpm_blocked", blocked);',
                 'dpm_stall_console();', 'sched_show_task(s);', 'show_state_filter(TASK_UNINTERRUPTIBLE);']
        positions = [sample.index(x) for x in order]
        self.assertEqual(positions, sorted(positions))
        # Idle workers are not counted as blocked tasks.
        self.assertIn('TASK_NOLOAD', sample)
        # Only the RAM console is un-suspended; UART/display console PM is left alone.
        console = function(self.source, 'dpm_stall_console')
        self.assertIn('"ramoops"', console)
        self.assertIn('console_srcu_write_flags', console)
        self.assertNotIn('console_stop', console)

    def test_sampler_hangs_off_the_existing_stage_mark_without_a_new_caller(self):
        mark = function(self.source[self.source.index('void y2_pm_mark'):].replace('void y2_pm_mark', 'static void y2_pm_mark', 1), 'y2_pm_mark')
        self.assertIn('thaw_stall_mark(stage, error);', mark)
        self.assertIn('INIT_DELAYED_WORK(&dpm_stall_work, dpm_stall_sample);', self.source)
        stall = function(self.source, 'thaw_stall_mark')
        self.assertLess(stall.index('dpm_stall_mark(stage, error);'), stall.index('Y2_PM_TASKS_THAWED'))


if __name__ == '__main__':
    unittest.main()
