# Y2Linux 1.0 Beta 9.2.12: Bluetooth switches off at once, readable grey text in light skins

Release `1.0.0-beta.9.2.12`, Y2Reborn `1.0.0-beta.9.2.12`, kernel `6.18.56-y2linux-1.0.0-beta.9.2.12`, update sequence
12800, root `2026.08-platform-v1.226`. Published as GitHub release `baseline-203` (test build 203). Previous
release: [Beta 9.2.11](Y2-LINUX-1.0-BETA-9.2.11.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

Switching Bluetooth off could take up to 20 seconds while your saved headphones were off, and pressing the switch
again in the meantime turned Bluetooth back on, so it kept cycling on and off. It now switches off at once. And
since Beta 9.2.3 the grey text (Cover Flow's details, hints, values) in light skins such as Bunny Bow was the light
grey of the dark design instead of the skin's own grey, which made it harder to read; it is the skin's colour again
(Y2Reborn #76).

## What changed

- **Bluetooth off:** while Bluetooth is on, the player keeps trying to reach your saved headphones; when they are
  switched off, each try takes about 18–20 seconds to fail. Switching Bluetooth off waited for that try to end, the
  switch did not move, and further presses were queued: once the switch-off went through, the next press turned
  Bluetooth on again. Now switching off goes through immediately and stops the try. Switching on waits as before.
- **Going to sleep** switches Bluetooth off first; that was slow for the same reason and is quicker too.
- **Grey text in light skins** (Y2Reborn #76): Beta 9.2.3 made the grey text brighter for dark screens, but skins
  kept getting that new brighter grey instead of their own. In Bunny Bow, Paper, Cassette, Classic and the Mini
  skins it was pale on a pale background. Every skin's grey text uses the skin's colours again.

## How it was tested

- **Before release:** the platform's full check, Reborn's workspace tests and the tests of the finished release
  images. New tests: switching Bluetooth off while a reconnect try is running goes through at once and stops the
  try, switching on still waits; Reborn's text, grey and dim colours all take a light skin's colours (this test
  fails without the fix).
- **On the developer's player (Beta 9.2.11):** switching Bluetooth off in the middle of a reconnect try took
  0.1 seconds instead of about 20; the try ended at once and the reconnect carried on normally afterwards.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to Beta 9.2.11.

## Known issues

Everything listed for [Beta 9.0](Y2-LINUX-1.0-BETA-9.0.md#known-issues-and-what-still-needs-your-help) still applies.

## Technical appendix

- Linux `3a839b94`: `y2-bt-reconnect` pages an absent device for ~20 s holding `bt-operation.lock`; the
  `power_pending` Disconnect abort does not end the page on the MT6582 controller, so `bluetooth-power off` answered
  busy until the page timed out and Reborn retried (up to 45 s). Power off now writes `power_off` and runs
  `bluetoothctl power off` without the lock; on still waits. The sleep helper's radio quiesce gains the same.
- Reborn `34e3d26`: `skin::remap`'s table had the pre-9.2.3 `#A0A4AE` / `#848894`; it now takes `color::MUTED` and
  `color::DIM` from the theme.
