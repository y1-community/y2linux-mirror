# Y2Linux 1.0 Beta 9.2.11: Wi-Fi and Bluetooth icons only while connected

Release `1.0.0-beta.9.2.11`, Y2Reborn `1.0.0-beta.9.2.11`, kernel `6.18.56-y2linux-1.0.0-beta.9.2.11`, update sequence
12700, root `2026.08-platform-v1.225`. Published as GitHub release `baseline-202` (test build 202). Previous
release: [Beta 9.2.10](Y2-LINUX-1.0-BETA-9.2.10.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

The Wi-Fi and Bluetooth icons at the top of the screen now show only while they are connected, as on a phone.
Before, they showed whenever Wi-Fi or Bluetooth was switched on, just dimmer when nothing was connected, and that
looked as if the player were connected when it was not (Y2Linux #73).

## What changed

- **Wi-Fi icon:** shown while the player is joined to a network.
- **Bluetooth icon:** shown while headphones or a speaker are connected.
- Whether Wi-Fi or Bluetooth is switched on is in Settings, as before. This works the same in the standard look
  and in every skin.

## How it was tested

- **Before release:** the platform's full check, Reborn's workspace tests and the tests of the finished release
  images. New tests: in the standard look and in skins, a radio that is on but not connected draws exactly as if it
  were off, a connected one shows, and Wi-Fi that is switched off never shows (even right after it was connected).
- **On the developer's player:** installed over the air from Beta 9.2.10 and confirmed by the player itself
  (Acknowledged); the platform and Reborn running, health OK, kernel not tainted.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to Beta 9.2.10.

## Known issues

Everything listed for [Beta 9.0](Y2-LINUX-1.0-BETA-9.0.md#known-issues-and-what-still-needs-your-help) still applies.

## Technical appendix

- `components/status.rs`: Bluetooth when powered and a device is connected, Wi-Fi when powered and
  `WifiStatus::Connected`. Skins: `indicators` use `bluetooth_connected` / `wifi_connected`; `Flag::WifiConnected`
  now also needs the radio on (an off radio with a stale Connected status showed the icon). The indicators' `dim`
  colour is still accepted and checked (`deny_unknown_fields`), no longer used; docs/skins.md says so.
