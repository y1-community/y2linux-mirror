# Device-resident power qualification harnesses

For DEEPSLEEP-FINAL-02, use **`deepsleep-clean-session.py`** after verifying
the installed source pair, USB/charging and taint zero. This collector checks
that exact identity and requires the captured boot ID. It takes a live
maintenance lock so automatic product sleep cannot race the owner helper
(which itself needs the activity lock). It also holds a durable product
quarantine through completion or failure, including resets. Clear that guard
only after the host has reviewed a real clean same-boot result; leaving it
pending would prevent the subsequent automatic-policy test.

The staged Power plan uses `--stages --wake power --alarm 300 --entry-after 180`
and schedules the one real entry against BOOTTIME. Prompt a short Power press
200 seconds after physical unplug. If the stage/archive deadline is missed,
the collector stops without a real entry. One-shot later trials omit `--stages`
and use an adequately long `--entry-after` for settling/archival. No retry is
automatic. Copy the current observer and qualifier beside the collector.
After reviewing a clean full return, `--cycle-check --wake power --alarm 300
--entry-after 180` runs three short RTC cycles, scheduled Power wake, then a
310-second RTC alarm. The last trial must accumulate at least 300 seconds in
the hardware SPM counter. Prompt Power at 200 seconds after unplug; reconnect
only after the final measurement window (normally ten minutes). This mode
refuses staged or module-observed runs and stops at the first failed check.

After host review of that clean batch, `deepsleep-product-session.py --boot-id
<current-boot> --reviewed-cycles <batch-directory> --product-source-sha256
<reviewed-live-helper-hash>` observes the normal product
path. It tests connected refusal, establishes screen-off through normal Power
behavior, holds a real maintenance flock beyond the inactivity timeout, then
releases it and waits for automatic suspend/RTC wake. It never calls the owner
suspend helper or writes `/sys/power/state`. Copy `power-screen.py` beside it.
The product helper supplies the existing backstop; an RTC300 alarm bounds the
trial. Request unplug only after `ready.json` exists. Reconnect after six minutes,
review its independent verdict and retain the restored diagnostic quarantine.
It records any live userspace overlay separately from the sealed image identity.
Archive53 found a buffered Python close replaying a refused suspend. The
unbuffered correction is commit0ba99f5; require its reviewed deployed bytes for
the corrective repeat, and never count53 as a successful product receipt.
Archive59 passes after disabling wake permission only on the synthetic USB
budget supply. Commit9066f73 makes that distinction in the kernel; the tested
live setting and userspace overlay are recorded separately from sealed02.

The next admitted mode adds `--suspended-seconds 1800 --reviewed-product
<successful-short-product-directory>`. It uses an RTC alarm with180 seconds
of setup allowance, requires the requested actual accumulated SPM residence,
and verifies every600-second maintenance return kept the screen/radios asleep.
Use a34-minute unplug window for this mode, beginning at the owner's unplug
reply. Never reconnect early. The120-minute and8-hour modes accept7200 and28800;
each requires reviewed success at the preceding duration and a fresh audit.
Only the thirty-minute mode is admitted as of archive61; its result is pending.

Deploy reviewed3929026 receipt/journal changes before longer measurements:
`first_entry` must survive maintenance cycles and completed endpoints must
survive a later refusal. The observer also captures `battery-after.json` and
requires a battery-daemon sample taken after the terminal resume, at most ten
seconds old. Immediate kernel-return receipt SOC may still be the pre-sleep
sample; preserve it but never use it alone as the final SOC endpoint. All
diagnostic journals are event-based or use the existing power-journal cadence.

The older `deepsleep-single-session.py`/`deepsleep-endurance-session.py` require
tainted 04 diagnostic modules and must not be used unchanged on the clean image.

Run on the Y2 from `/run/y2` (copied there over SSH; `/run` is tmpfs). They survive
USB removal because they run detached (`setsid nohup python3 ... &`) and write to
`/data/logs/power-final-02-*`.

- `suspend-session.py` waits for USB unplug, blocks Wi-Fi at runtime, runs 60 s and 300 s idle
  windows, the five staged pm_test stages and ONE bounded RTC suspend (backstop 30 s).
  Needs `suspend_platform_device.py` and `qualify-platform-v1-suspend.py` from
  `tools/development/` copied beside it, plus `power-screen.py` and `dac-readback.sh`.
- `battery-idle-session.py` Wi-Fi CAM vs FAST_PSP (5 min each) then 30 min radios-off.
- `dac-readback.sh`, `proc-rates.sh`, `power-press.sh` read-only probes (DAC rails, per-process
  wake rates, synthetic Power presses).

Take `/tmp/y2-platform-v1-device.lock` first. Never retry a suspend entry. Candidates for merging
into the existing `qualify-*` tools once the suspend stall is fixed.
