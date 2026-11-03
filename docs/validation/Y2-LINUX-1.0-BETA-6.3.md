# Y2Linux 1.0 Beta 6.3 (Baseline 60)

Status: **PUBLISHED as GitHub release `baseline-60`** (2026-10-16). Release `1.0.0-beta.6.3`, Reborn `1.0.0-beta.6.3`, kernel
`6.18.56-y2linux-1.0.0-beta.6.3`, update sequence 5750, Linux `b20404c`, Reborn `b18d98c`, git tag `baseline-60` in both
repositories. Built with `tools/production/release.py` (`--public`) in 120 s: container suite **740 OK**, all seal checks PASS,
no MediaTek firmware in any image. Milestone audit: BETA-6.3 in [roadmap-gap-audit](../planning/roadmap-gap-audit.md).

| Image | SHA-256 |
| --- | --- |
| BOOTIMG | `6cbfcc1b7fe5e6c181182d129eec6bb1dca0098e4bdfbdf3ea49eb26f6c7dae8` |
| Y2ROOT | `5544c31dde9e116cabd03d5e7d20efcd40352bac53c6134a31243a6d246cb450` |
| RECOVERY | `ccf55136e2ba99dc6508b6a9e3018fc909faa92b29b5cf4bb0722b52750ddeb2` |
| Y2DATA (no SSH key) | `c5b93826fe295a31059355cc3d878d027feb132e694a1e22e0d8a2b22084e852` |

## Since Beta 6.2 (Reborn)

Owner (2026-10-16): "inspect why when i presse power this does not go to sleep finishing another tadsk"; "yes" (publish).

| Part | Detail |
| --- | --- |
| Evidence | Device on 6.2, battery, music paused at 22:57:42 UTC. `/data/system/platform/power-journal.jsonl`: `suspend_requested` → `suspend_product_refused` with `activity_operation:808/reborn` at 22:58:00, :13, :20 and :28 (PID 808 = Reborn). Reborn maps reasons containing `activity_operation`/`in_progress` to "The player is finishing another task". Neither side logged the reason to the user-visible log. |
| Cause | An open sink holds a shared flock on `/run/y2/activity.lock` (`crates/reborn-audio/native/audio.c`, `rb_sink_open`). Pause in place (Beta 6) keeps the sink open up to 3 minutes. `product_sleep.execute_owned` takes the operation locks before it asks Reborn to prepare (`sleep_prepare`), so Reborn's own release in `prepare_sleep` came too late. After 3 minutes paused (hold released) sleep worked. |
| Fix | A sleep request (Power or automatic) first releases a held pause and waits for the sink to close (`release_hold_with(true)` → `Playback::stop_and_wait`, ≤2 s); Play then restarts at the same place. Platform sleep refusals are logged (`power/sleep_refused`). |
| Not part of this | `charger_active` / `charger_or_usb_active` refusals while the cable is in (expected; the screen blanks instead). |

Host evidence: 474 Reborn tests, strict Clippy, fmt; 767 Y2Linux host tests. The runtime path has no host harness.

## Device checks owed (after the owner's over-the-air install)

On battery: pause, press Power within a few seconds → the player sleeps; Power again wakes it and Play continues at the same
place. The Reborn log shows `pause_released` with `for_sleep: true`; a refusal (e.g. with the cable in) shows `sleep_refused`.
Plus the Beta 6.2 and 6.1 checks.

## Known gaps

As Beta 6.2: FM radio (next beta); no 12/24-hour switch; in-place decoder seek (≈1 s on Bluetooth); sleep ≈9.4 mA vs stock
≈1.3 mA; Bluetooth range hardware-limited; no headphone-jack detection.
