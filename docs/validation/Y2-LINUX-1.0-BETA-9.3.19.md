# Y2Linux 1.0 Beta 9.3.19: update notifications, if you want them

Release `1.0.0-beta.9.3.19`, Y2Reborn `1.0.0-beta.9.3.19`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.19`, update
sequence 15300, root `2026.08-platform-v1.251`. Published as GitHub release `baseline-228` (test build 228). Previous
release: [Beta 9.3.18](Y2-LINUX-1.0-BETA-9.3.18.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

**The "Software update available" message works again, and it is now your choice** (Y2Reborn #97). The player has
always looked for a new version once a day while Wi-Fi is on, but the message almost never appeared. It now does,
once for each new version, if you turn on **Settings → System → Software Update → Update Notifications**. It is off
by default, so nothing changes for anyone who would rather not be told.

## What changed

- **Update Notifications** (Y2Reborn #97): a switch at the bottom of Software Update, off by default. When it is on
  and the player's daily check finds a newer version, the status bar says "Software update available" for a moment,
  on whatever screen you are on. Each version is named once: not again after a restart or every time Wi-Fi connects.
  On the Software Update page itself there is no message, since the page shows the update.
- **Why the message had disappeared:** the daily check kept working, but the music player only read its answer while
  you were in Settings. So the message came only if you happened to open Settings after the check, and most people
  never saw it.

## How it was tested

- **New tests:** when the message is shown, kept quiet or recorded (switch off, version already named, Software
  Update on screen, no update), when the player looks for the check's answer, and the switch on Software Update.
- **On a player:** test build 228 installed by update on the developer's player. A check result offering a newer
  version was placed on the player while it showed the Music screen:
  - switch off, 150 seconds with the screen on: no message;
  - switch on, a version already named: no message;
  - switch on, a new version: the message appeared after 64 seconds and was captured in a screenshot; with the
    screen off, it appeared 3 seconds after the screen woke.
- **Before release:** the platform's full check, Reborn's workspace tests and the tests of the release images.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.18](Y2-LINUX-1.0-BETA-9.3.18.md#known-issues) still applies.

- With Update Notifications on, the player looks for the daily check's answer every two minutes while Wi-Fi is on
  and the screen is lit, so the message can take up to two minutes after the check (or comes when the screen wakes).

## Technical appendix

- Reborn `e989efb`: `Settings.update_notifications` (default false) and `update_announced` (the version last named,
  persisted). `main_loop.rs` observes the platform on any screen when `update_watch_due` (notifications on, online,
  `UPDATE_WATCH` 120 s since the last observation, screen on); before, only `platform_backed` screens and one
  observation 20 s after start did, so the daily `update auto-check` result (download.json `Checked`) reached Reborn
  only in Settings. `update_notice` decides Show / Record (Software Update on screen) / None. The toggle row is added
  by `pages::update_status` under every Software Update page that has rows. Tests in
  `main_tests/update_screen_tests.rs` and `reborn-ui` tests.
- The platform needs no change: a release older than the installed one is already reported as up to date (a
  sequence downgrade writes the installed version).
- Linux `d93d51e9`: code-quality baseline exception (glue lines in files already over the ceilings). Bump `520c4b8`,
  `d6c404e4`.
