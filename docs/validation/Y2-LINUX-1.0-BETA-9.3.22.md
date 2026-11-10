# Y2Linux 1.0 Beta 9.3.22: the screen no longer comes on by itself after a sleep that didn't work

Release `1.0.0-beta.9.3.22`, Y2Reborn `1.0.0-beta.9.3.22`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.22`, update
sequence 15800, root `2026.08-platform-v1.256`. Published as GitHub release `baseline-233` (test build 233). Previous
release: [Beta 9.3.21](Y2-LINUX-1.0-BETA-9.3.21.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

A small follow-up to 9.3.21. **The screen no longer turns on by itself every few minutes** on a player that tries
to go to sleep and can't (reported for 9.3.13 to 9.3.16). When a sleep fails before the player has actually gone to
sleep, the screen now stays dark, as it already did when a sleep was refused.

## What changed

- **A failed sleep stays dark**: the player goes to sleep by itself when it is paused and idle. If that attempt
  failed, the screen was switched on again, and with every attempt failing it came on again and again, every few
  minutes, in a pocket or on a desk. It now stays dark, and the next attempt waits longer. Pressing Power during
  the attempt still lights the screen at once, and so does waking from a real sleep.
- **Why a sleep can fail at all**: one cause was fixed in 9.3.21 (players whose Wi-Fi and Bluetooth never started).
  This change makes any other cause quiet as well, and what failed is still recorded for an error report.
- **For people who report issues**: every issue form now asks you to search first, and there is a page of
  [ideas that won't be built, and why](../NOT-PLANNED.md).

## How it was tested

- **Tests** for the two situations in which a sleep fails before the player sleeps (the Bluetooth step fails, the
  preparation cannot be recorded) now expect the dark screen; all sleep tests, the platform's full check, Reborn's
  workspace tests and the tests of the release images passed.
- **On a player** (the developer's): this release installed by update, started, and Wi-Fi and Bluetooth came up as
  on 9.3.21.
- **Not tried:** a failing sleep on a player. It cannot be provoked on a healthy one.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.21](Y2-LINUX-1.0-BETA-9.3.21.md#known-issues) still applies.

- A player that cannot sleep still uses more battery than one that can; if the battery drains overnight, send an
  error report (Settings → System → Error Reports).

## Technical appendix

- Linux `c001e8ad`: `suspend/helper.rs` `finally`: the restore is dark for an unwoken refusal (as since F47) and now
  also for a failure with the kernel never entered (`self.result == 1 && !self.attempted`); a failure after entry
  and `wake_event_in_progress` restore lit. Golden `product-sleep-sync-fails` re-recorded (`dark: true` in the
  restore request). Reborn handles a dark restore as a refusal (`sleep_refused_dark`, backoff).
- Linux `7ae193a1`, Reborn `47b7a42`: issue forms with a required "searched" box, `docs/NOT-PLANNED.md`,
  `.github/workflows/similar-issues.yml` (a comment with similar earlier issues).
- Bumps `63a2052`, `bc043e92`. Reported in Y2Player #132; related Y2Reborn #116.
