# Y2Linux 1.0 Beta 9.3.24: standard SBC for everyone, and the right battery level after charging while off

Release `1.0.0-beta.9.3.24`, Y2Reborn `1.0.0-beta.9.3.24`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.24`, update
sequence 16000, root `2026.08-platform-v1.258`. Published as GitHub release `baseline-235` (build 235). Previous
release: [Beta 9.3.23](Y2-LINUX-1.0-BETA-9.3.23.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

Two changes that came out of error reports. **Bluetooth now always sends standard SBC**: the XQ and XQ+ choices are
gone, because they made music stutter ([Y2Reborn #123](https://github.com/SchulzCode/Y2Reborn/issues/123)). And
**the battery shows the right level after charging while switched off**
([#113](https://github.com/SchulzCode/Y2Linux/issues/113)).

## What changed

- **SBC XQ is gone.** XQ and XQ+ were choices in the service menu that sent much more data over Bluetooth than
  standard SBC. A report from a player set to XQ showed two speakers stuttering while the player itself sent every
  piece of music on time: the radio link, which Bluetooth shares with Wi-Fi, could not carry that much. The **SBC
  Quality** row is removed. A player that was set to XQ uses standard SBC after the update; there is nothing to do.
  **SBC Auto Rate** stays where it was, and AAC and LDAC are unchanged.
- **Battery level after charging while switched off.** A player switched off at 63 %, charged while off and started
  the next day without the charger still showed 63 %, although the battery was nearly full. It looked like the
  battery had drained while off; it had not. The player now shows the level it measures when it starts.
- **For people who send error reports**: a report now holds Bluetooth's data rate over time, so a stutter can be
  matched to what the link did at that moment ([#99](https://github.com/SchulzCode/Y2Linux/issues/99),
  [Y2Reborn #87](https://github.com/SchulzCode/Y2Reborn/issues/87)).

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests cover
  a saved XQ setting (read as standard SBC, Bluetooth still starts) and a start after charging while off (63 % saved,
  nearly full measured: the measured level is shown; a restart without switching off keeps the saved level).
- **Not tried:** this release on a player.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.21](Y2-LINUX-1.0-BETA-9.3.21.md#known-issues) and
[Beta 9.3.22](Y2-LINUX-1.0-BETA-9.3.22.md#known-issues) still applies. New:

- Music can stutter for a few seconds while Wi-Fi switches on (seen in two reports on Beta 9.3.23, wired and
  Bluetooth). The cause is not found yet.

## Technical appendix

- Linux `6cd59c94`: `codec.rs` has one SBC quality (`SBC_QUALITY`); a saved `xq`/`xq+` is read as `high`;
  `codec-settings --sbc-quality` takes `high` only; y2-radiod's `CODEC_ARGUMENTS` has no XQ argument; capability
  `bluetooth_sbc_xq` stays and is never enabled. Reborn `97b05c5`: SBC Quality row, `SbcQuality`,
  `PlatformTask::SbcQuality` and the SBC-XQ codec choice removed; a saved `SBC-XQ` preference reads as SBC.
  Evidence: report fff72d5d (install 4c5dcdcb, 9.3.23).
- Linux `6cd59c94`: `power/battery/display.rs` `restore()`: on a cold start whose fresh power-on OCV reads 10 or more
  points above the saved display value, the OCV estimate is shown (`UI_CHARGED_OFF_POINTS`, finding F81). Evidence:
  report fc0b70b9 (install a204f45a, 9.3.23): off at 63 % / 3.82 V, cold start at an OCV of 4.114 V, `cold/saved`.
- Linux `abe9918e`: y2-radiod `rate.rs` writes each change of BlueALSA's EncoderStats while a stream plays to
  `/run/y2/bt-encoder-rate.jsonl` (tmpfs, 128 KB, part of every report).
- Open, finding F80: underruns while the Wi-Fi radio starts (reports 9862ade7, 91bef6ce).
- Bumps `0537b06`, `e0a57350`.
