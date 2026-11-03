# Suspend state probe — test plan (proposed, not yet run)

Goal: obtain the real PMIC/regulator/clock/GPIO state **just before and just after** sleep on DEEPSLEEP-FINAL-04 and compare it with the awake baseline
(archive 78) and the vendor suspend golden table (25 of 44 entries differ awake). This is evidence for ranking candidates; it is not a measurement of
current and not a reading taken while the SoC sleeps.

## What the probe does (read-only)
`tools/development/power-session/suspend-probe.py` runs on the Y2 (stdlib Python, copied to tmpfs `/run/y2`). It reads the PMIC register dump
(`/sys/kernel/debug/regmap/1000d000.pwrap/registers`, ~26 ms), regulator and clock summaries, GPIO, wakeup sources, SPM state, runtime-PM status. Captures:
`pre-entry` (journal `suspend_syncing`, 0.1–0.7 s before freeze), `maintenance` (0.8 s after each maintenance wake), `post-resume` (first userspace instant after a
BOOTTIME jump). A capture during which a suspend began is flagged `suspend_during_capture`. It never writes PM/PMIC/RTC/guard/wakeup state and holds no wakeup source.
Captures go to `/data/logs/suspend-probe-<boot>-<time>/NNN-label.json` (small; ~60 KB each, max 24).

## Device changes if approved
Only: the probe file and log on tmpfs `/run/y2`, and its capture directory under `/data/logs`. No product, kernel, helper or policy change; the 04 image is untouched.

## Procedure
1. Owner: connect USB, device on the player, paused. Claude: archive first (`capture-return.py`), then `arm-suspend-probe.py` under the host lease.
2. Owner: screen dark, **unplug**, tell Claude the time, wait **~25 minutes untouched** (>= 2 maintenance wakes; sleep starts ~40–60 s after darkness).
3. Owner: re-plug and tell Claude. Claude: archive first, then analyse: `tools/development/analyze-suspend-probe.py <captures> --baseline <archive 78 regs.txt>`.
4. Compare: rails on at pre-entry/maintenance/post-resume; LP_SEL bits; golden differences remaining; enabled clocks; SD/eMMC/connectivity/USB runtime-PM state.

## Stop conditions / safety
Probe self-limits (`--max-seconds` 3600, 24 captures) and stops on boot change. If a stall/reset occurs, do not restart; connect USB and archive (04 stall records apply).
Do not read `/sys/power/wakeup_count` or `/proc/kmsg` over SSH.

## Interpretation limits
Registers read at pre-entry are the state before the freezer/SPM acts; the SPM/PCM may change PMIC state during sleep and restore it before post-resume. A
difference between pre-entry and the golden table is therefore a **candidate**, not proof of a leak. A true during-sleep current needs an external measurement.
