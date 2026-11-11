# Y2Linux 1.0 Beta 9.3.23: the progress wave takes the cover's colour

Release `1.0.0-beta.9.3.23`, Y2Reborn `1.0.0-beta.9.3.23`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.23`, update
sequence 15900, root `2026.08-platform-v1.257`. Published as GitHub release `baseline-234` (build 234). Previous
release: [Beta 9.3.22](Y2-LINUX-1.0-BETA-9.3.22.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

A small look change. **The progress wave on Now Playing now has the colour of the album cover**, as the small
player on Home already had ([#108](https://github.com/SchulzCode/Y2Linux/issues/108)). There is no setting for it:
it simply follows the cover.

## What changed

- **Now Playing's progress wave wears the cover's colour**: it used to be white. The colour is the same one Home's
  card, the format badge and the glow behind the cover already take from the cover. A skin with a fixed accent
  colour uses that colour instead. The knob stays white, and a radio station keeps its softer tint.
- **For people who report issues**: every issue form now asks how you installed Y2Linux (SP Flash Tool or the
  Innioasis Updater) and whether Wi-Fi and Bluetooth work. The install form is now "Install, start or update" and
  covers "Couldn't Start": which tool, which computer, whether stock was flashed first, and the SD card.

## How it was tested

- **Tests**: Reborn's interface tests, the platform's full check and the tests of the release images passed.
- **Preview**: all 204 screens were drawn on a computer. On Now Playing, playing and paused, the wave has the cover's
  colour and matches Home.
- **Not tried:** this release on a player.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.21](Y2-LINUX-1.0-BETA-9.3.21.md#known-issues) and
[Beta 9.3.22](Y2-LINUX-1.0-BETA-9.3.22.md#known-issues) still applies.

## Technical appendix

- Reborn `30cbdda`: `views_playing.rs` passes `ui.cover_accent()` to `components::wave_progress` for songs (was
  `color::TEXT`); radio keeps `mix(accent, BG, 0.3)`. Owner decision: no toggle.
- Linux `5390caaa`, Reborn `34011b9`: issue forms (`install`, `radios_work`; `6-update.yml` "Install, start or
  update"), `.github/workflows/needs-info.yml` (label off on the author's reply, one reminder after 14 days),
  `tools/reports/summary.py` (first-look facts per error report).
- Bumps `9930022`, `8f357e92`.
