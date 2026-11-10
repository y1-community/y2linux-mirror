# Y2Linux 1.0 Beta 9.3.21: steadier Bluetooth on a weak signal, Wi-Fi on channels 12 and 13, two battery and SD card fixes

Release `1.0.0-beta.9.3.21`, Y2Reborn `1.0.0-beta.9.3.21`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.21`, update
sequence 15700, root `2026.08-platform-v1.255`. Published as GitHub release `baseline-232` (test builds 230 to 232).
Previous release: [Beta 9.3.20](Y2-LINUX-1.0-BETA-9.3.20.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

This release comes out of a day of measuring Bluetooth on a real player. **When the Bluetooth signal gets weak, the
music no longer drops out and then plays fast**: the player used to send everything it had missed in one burst
(Y2Reborn #87, probably #99). **Bluetooth now keeps off the channel your Wi-Fi uses.** **Wi-Fi networks on
channels 12 and 13 are found** without any setting (#110). **A player whose Wi-Fi and Bluetooth never started no
longer stays awake all night** (Y2Reborn #116). **Reading a damaged file on an exFAT SD card no longer freezes the
card.** FLAC and Opus songs **show a bitrate** (#87).

## What changed

- **Bluetooth on a weak signal** (Y2Reborn #87, #99): measured with the player moved away from the headphones until
  the sound broke up, the link paused up to 2.9 seconds, and the player then sent all the audio it had missed as
  fast as it could, six seconds of music in three. Headphones answer that with a dropout and fast playback. Now at
  most 0.2 seconds is made up after a pause in the link, gently; the music simply continues where it stopped.
- **Bluetooth never goes quiet in the middle of a stream**: if the player is busy for a moment and delivers no
  audio, silence is sent in its place after a tenth of a second, so the headphones always receive something while
  music is on. Some headphones (AirPods among them) went silent after such a gap until the next song.
- **Bluetooth keeps off your Wi-Fi's channel**: Wi-Fi and Bluetooth share one antenna. While Wi-Fi is connected,
  Bluetooth is told which channels the Wi-Fi network uses and leaves them alone. Before, it left out only a few of
  them.
- **The audio runs first**: the parts that prepare and send audio now run ahead of the player's other work (a
  library scan, covers, copying over USB).
- **Slow headphones get their time**: starting music waited 5 seconds for Bluetooth headphones to answer and then
  gave up, although headphones may take 6. It now waits 8.
- **Wi-Fi on channels 12 and 13** (#110): the player has no country setting, and without one these two channels may
  only be listened on until an access point is heard there. The player skipped them completely, so a router on
  channel 12 or 13 never showed up. Every scan now also listens on them; once your router is heard, the channel is
  used like any other.
- **A player without radios sleeps** (Y2Reborn #116): on a player whose Wi-Fi and Bluetooth never start, every
  sleep failed at the step "switch Bluetooth off", so it stayed awake and the battery was empty by morning.
- **exFAT SD cards**: reading a file whose data ends in an odd place (an interrupted copy, for example) could crash
  inside the system and freeze the whole card until a restart. Fixed.
- **FLAC and Opus have a bitrate** (#87), which skins can show. For songs already in your library, run
  Settings → System → Reset & Maintenance → **Rebuild Music Library** once.

## How it was tested

- **On a player** (the developer's, with AirPods Pro): before the changes, ten-minute recordings of the Bluetooth
  stream on the desk, with the player moved away, during Wi-Fi scans and downloads, and with the processor and the
  SD card busy. With this release: music over Bluetooth through play, a short pause, a long pause, a seek and
  several skips, with a packet every 23 milliseconds and never a gap over 40; Wi-Fi scanning and connecting; the
  Wi-Fi channel left out by Bluetooth within two seconds; every file on an exFAT card read without the freeze.
- **Found on the way and fixed before release:** the first version of the channel 12 and 13 change made the system
  log an error when a network was heard there (test build 231); the release has the corrected version.
- **New tests** for each change, and the platform's full check, Reborn's workspace tests and the tests of the
  release images.
- **Not yet tried:** the weak-signal test again with this release (the fix was checked on a computer against the
  recorded pauses); joining a router on channel 12 or 13 (a neighbour's network on channel 13 appeared once during
  testing, then was out of range); a night on a player without radios; other headphones than AirPods.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.20](Y2-LINUX-1.0-BETA-9.3.20.md#known-issues) still applies.

- A network on channel 12 or 13 may need a second scan to appear: the player only listens there, briefly.
- Far from the headphones the music still pauses when the signal is lost; it no longer rushes afterwards.
- Skins that draw their own lists scroll row by row, without the glide of Reborn's own lists (Y2Reborn #114).

## Technical appendix

- Linux `e8b6a145`: BlueALSA patches `0009` (`src/shared/y2-catchup.h`: at most 200 ms owed after a stall, sent at
  no more than 4/3 of real time; daemon and ALSA plug-in) and `0010` (`src/y2-filler.h`: an A2DP source stream whose
  client has played gets silence after 100 ms without audio, until audio, drain or close); `--io-rt-priority=1`;
  `tools/connectivity/bt-afh.c` = `/usr/sbin/y2-bt-afh` and the service daemon's `daemon/channels.rs` (AFH host
  channel classification from wpa_supplicant's `freq`, on association, Bluetooth power-on and every 30 s; record
  `/run/y2/bt-channel-mask.json`); kernel patch `0072` (`exfat_get_block` zeroes the unwritten rest of a block with
  `folio_zero_segment`, not through `b_data`, which is only an offset for a highmem folio).
- Linux `c263fd89`: `y2-radiod ctl` quiesce without the daemon succeeds when the radio core's Bluetooth function is
  off. `c1102671`, `a82907a4`: `wifi/y2_listen.h`, `regulatory.c` (listen-only channels in the firmware's domain, a
  second `SCAN_TYPE_PASSIVE_SCAN` pass per scan, cfg80211's flags read before each ordinary pass), beacon hints on
  (`regulatory_flags = 0`), no notifier call for a hint (it waits; cfg80211 calls it under a spinlock).
- Reborn `cd5bd45`: `reborn_platform::audio_thread_priority` (SCHED_FIFO 1) for the sink thread;
  `BLUETOOTH_OPEN_DEADLINE` 8 s. `b42c10f`: `media.c` bitrate from file size over length when stream and container
  name none.
- Measurements and method: `docs/architecture/radio-daemon.md` (last section). Bumps `c099b38`, `8fb7b826`.
