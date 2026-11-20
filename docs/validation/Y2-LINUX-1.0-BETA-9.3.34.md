# Y2Linux 1.0 Beta 9.3.34: Bluetooth stalls in error reports, and where Bluetooth's limits are

Release `1.0.0-beta.9.3.34`, Y2Reborn `1.0.0-beta.9.3.34`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.34`, update
sequence 17200, root `2026.08-platform-v1.290`. Published as GitHub release `baseline-267` (build 267). Previous
release: [Beta 9.3.33](Y2-LINUX-1.0-BETA-9.3.33.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

A last look at every open Bluetooth issue and its error reports (Y2Linux #99, #129, #130, Y2Reborn #132) found the
player's side clean in all of them: no audio underruns, no codec errors, the rate and channel protections working.
What remains is the radio link itself. This release adds the one thing that was missing to tell such cases apart:
each stall of the link is now written down, so a report shows when a cutout happened and how long it lasted.

## What the reports showed

- **Soundbar stutter (#99)**: the rate stepped down six times in 20 seconds while Wi-Fi was off the whole time. The
  link to that soundbar is weak; holding the player or moving it makes it worse. Wi-Fi was not the cause.
- **Earbuds cutting out in a pocket (#130)**: short cutouts that recover by themselves, no disconnects, no rate
  changes. A player in a pocket, covered by a hand, is the hardest case for a 2.4 GHz radio.
- **AirPods silent in crowded places or after walking away (#129, #132)**: the link stalls without disconnecting;
  afterwards some AirPods stay silent until a pause and play start a new stream. Reborn's own writes keep flowing
  during such a stall, so the player cannot notice it from the music side; restarting the stream from inside the
  Bluetooth service would change the path for every pair of headphones and could not be tested out of range, so it
  is not done.

## What Y2Linux already does about it

Lower data rate when the link struggles (SBC Auto Rate, LDAC), a lower codec ceiling while Wi-Fi is on, Bluetooth
kept off the Wi-Fi channel, Wi-Fi power save while music streams, a bounded catch-up after a stall, silence instead
of gaps, and real-time priority for the audio path. Beyond that the limit is the hardware: one small radio sharing
its antenna with Wi-Fi. **For listening without dropouts, wired headphones are the best choice on the Y2.**

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. The changed
  BlueALSA patch applies to BlueALSA 5.0.0 with the other Y2 patches, and its catch-up tests pass; the report
  summary counts the new lines (new test). Only a log line is added: the pacing is unchanged.
- **Not tested on a player**: a real Bluetooth dropout with this release, and this release itself.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

- AirPods can stay silent after a Bluetooth dropout until you pause and play
  ([Y2Reborn #132](https://github.com/SchulzCode/Y2Reborn/issues/132)).
- Everything else listed for [Beta 9.3.32](Y2-LINUX-1.0-BETA-9.3.32.md#known-issues) still applies.

## Technical appendix

- BlueALSA patch `0009`: `asrsync_sync()` warns `Y2 stall: stream N ms behind real time` once per stall it bounds
  (when the start time moves); `bluealsad` runs with `--syslog` (`y2-radiod` `bt/supervise.rs`), so the line lands in
  `/data/logs/system.log`. `tools/reports/summary.py` prints the count and the longest.
- Images built from Linux `d3ea1f73`, Reborn `a8c8ec8`.
