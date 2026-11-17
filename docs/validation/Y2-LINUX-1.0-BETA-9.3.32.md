# Y2Linux 1.0 Beta 9.3.32: newer memory chips, USB that comes back, the wheel click and films over Bluetooth

Release `1.0.0-beta.9.3.32`, Y2Reborn `1.0.0-beta.9.3.32`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.32`, update
sequence 16900, root `2026.08-platform-v1.286`. Published as GitHub release `baseline-263` (build 263). Previous
release: [Beta 9.3.31](Y2-LINUX-1.0-BETA-9.3.31.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

This release comes out of a pass over every open issue of both projects. It lets Y2s with a newer memory chip install
Y2Linux, brings USB back by itself after a dropped connection, fixes the wheel click after a USB transfer and keeps
films in step with Bluetooth headphones. Playlists get **Clear Playlist** and no longer take a song twice; album
pages show their year; skin makers get three new pieces.

## What changed

- **Stuck at "Preparing storage"** ([Y2Reborn #130](https://github.com/SchulzCode/Y2Reborn/issues/130)). Some Y2s
  carry a memory chip whose security area (RPMB) is 4 MB instead of 512 KB. Y2Linux knew only one chip layout and
  waited forever. Where the music area starts depends on that size: the stock firmware works it out as 20 MB minus
  both boot areas and the security area, and Y2Linux now does the same. Players with the chip known so far start
  exactly as before.
- **USB stayed dead until a restart** ([Y2Linux #117](https://github.com/SchulzCode/Y2Linux/issues/117)). A report
  showed the player's USB connection undone in the middle of a copy while the transfer program ran on; nothing set it
  up again, so the computer saw nothing even after replugging. The player now notices that and sets the connection up
  again. Why it was undone during that copy is not known yet.
- **The wheel clicked on every other row** ([Y2Linux #127](https://github.com/SchulzCode/Y2Linux/issues/127)). After a
  USB file transfer the music is stopped, and while stopped the player forgot each click right after starting it; the
  output ran empty and the next click was lost waking it up. A restart helped because it brings the player back paused.
- **Films ahead of the sound on Bluetooth** ([Y2Reborn #133](https://github.com/SchulzCode/Y2Reborn/issues/133)). The
  player's idea of where the sound is left out most of the Bluetooth delay, so the picture ran ahead by up to that
  delay. Now it counts it, and the time shown on Now Playing follows what you hear too.
- **Clear Playlist** ([Y2Linux #103](https://github.com/SchulzCode/Y2Linux/issues/103)): hold Select on a playlist,
  Favorites included. It asks first; the songs stay in your library.
- **No song twice** ([Y2Linux #106](https://github.com/SchulzCode/Y2Linux/issues/106)): Add to Playlist leaves out songs
  the playlist already holds and says how many.
- **Album pages** show the year first ("2019 · 12 songs · 48:10", [Y2Linux #125](https://github.com/SchulzCode/Y2Linux/issues/125)),
  and album and artist pages say **Play All** and **Shuffle All** ([Y2Reborn #145](https://github.com/SchulzCode/Y2Reborn/issues/145)).
- **Skins**: `{year}` ([#135](https://github.com/SchulzCode/Y2Reborn/issues/135)), `{page_title}` is the album's or
  artist's name on their pages ([#144](https://github.com/SchulzCode/Y2Reborn/issues/144)), and `"if": "scrollbar"`
  inside list rows ([#138](https://github.com/SchulzCode/Y2Reborn/issues/138)). See the skin guide.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: both
  memory chip layouts (every other one refused), USB set up again after an unbind, every wheel click after a stop on
  an output that runs dry, the position behind a long Bluetooth delay, the new skin values and condition, the album
  line and labels, Clear Playlist and songs already in a playlist.
- **On one player** (Beta 9.3.30): the click problem was reproduced (stopped, a click every 0.6 s: an underrun on
  every second one). Holding a volume button with the screen off, set to Next, skipped songs paused and playing
  (simulated presses). Copies of 545 MB and 2.5 GB over USB across the screen going dark finished.
- **Not tested on a player**: a Y2 with the 4 MB chip, a USB connection undone during a copy, films over Bluetooth,
  and this release itself.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

- AirPods can stay silent after you walk out of range and come back while the link holds; pause and play brings the
  sound back ([Y2Reborn #132](https://github.com/SchulzCode/Y2Reborn/issues/132)).
- Holds of the volume buttons with the screen off did nothing for one listener
  ([Y2Linux #124](https://github.com/SchulzCode/Y2Linux/issues/124)); not reproduced yet.
- Everything else listed for [Beta 9.3.30](Y2-LINUX-1.0-BETA-9.3.30.md#known-issues) still applies.

## Technical appendix

- Kernel `kernel/platform/storage-layout.h`: `y2_emmc_user_offset()` = 40960 − 256 × (2 × BOOT_SIZE_MULT +
  RPMB_SIZE_MULT); `y2_emmc_layout_valid` accepts RPMB_SIZE_MULT 4 or 32; patch 0009 keeps `y2_user_offset` per host
  and passes it to `y2_emmc_wire_arg`. The disk stays 15203328 sectors on both chips.
- Platform `service/usb.rs`: `lost_bind()` (empty UDC under a bound gadget) fails the attempt, so the responder stops
  and the next tick sets the gadget up and binds it.
- Reborn `playback/output.rs`: `stopped()` forgets clicks once per stop (`stop_seen`); `heard_ms()` = block start +
  written − delay, clamped at zero. `skin/`: `Var::Year`, `Flag::Scrollbar`, `{page_title}` from `source_label`.
  `catalog.rs` `page_detail`; `PlaylistEdit::Clear`, `ConfirmAction::ClearPlaylist`; picker skips resolved songs.
- Images built from Linux `91dc6a81`, Reborn `5e4b024`.
