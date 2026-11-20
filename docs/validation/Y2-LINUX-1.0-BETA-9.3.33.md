# Y2Linux 1.0 Beta 9.3.33: lyrics, Recently Added, Recently Played, Most Played and Shuffle All for Songs

Release `1.0.0-beta.9.3.33`, Y2Reborn `1.0.0-beta.9.3.33`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.33`, update sequence 17100, root
`2026.08-platform-v1.289`. Published as GitHub release `baseline-266` (build 266). Previous release:
[Beta 9.3.32](Y2-LINUX-1.0-BETA-9.3.32.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

This release brings lyrics to Now Playing, three new lists under Music (Recently Added, Recently Played and Most
Played) and Play All and Shuffle All at the top of Songs. Three changes come from contributors: lyrics from
Lord-Funion, and from jonasevcik the cover after a restart, a quieter start without "Scan finished" messages, and one
library scan at start instead of two.

## What changed

- **Lyrics** ([Y2Reborn #126](https://github.com/SchulzCode/Y2Reborn/pull/126)): Now Playing → hold Select →
  **Lyrics**. They come from a `.lrc` file next to the song or, without one, from the song's tags (MP3, FLAC, Ogg,
  M4A). Timed lyrics follow the music and seeking; plain lyrics scroll with the wheel. Nothing is looked up online.
  See the [lyrics guide](https://github.com/SchulzCode/Y2Reborn/blob/main/docs/lyrics.md).
- **Recently Added, Recently Played, Most Played** under Music
  ([Y2Reborn #109](https://github.com/SchulzCode/Y2Reborn/issues/109),
  [#158](https://github.com/SchulzCode/Y2Reborn/issues/158)), 100 songs each. Recently Added goes by the files' dates,
  newest first. A song counts as played once you have heard half of it or 4 minutes, the rule Last.fm uses; seeking,
  pausing and skipping add nothing. It counts with or without Last.fm, also for songs without tags; radio,
  audiobooks and films do not count. The history starts with this update and survives a rescan.
- **Play All and Shuffle All on Songs** ([Y2Reborn #163](https://github.com/SchulzCode/Y2Reborn/issues/163)) and on
  the three new lists; holding Select on them offers the whole menu (Shuffle Albums, Play Next, Add to Queue, Add to
  Playlist). Fast scrolling steps over them.
- **The cover after a restart** ([Y2Reborn #150](https://github.com/SchulzCode/Y2Reborn/pull/150)): Now Playing shows
  the last song's cover at once, not only after Play.
- **No "Scan finished" after every start** ([Y2Reborn #155](https://github.com/SchulzCode/Y2Reborn/pull/155)): a scan
  you start yourself still answers; the scans the player runs by itself only say when new songs were added.
- **One library scan at start** ([Y2Reborn #156](https://github.com/SchulzCode/Y2Reborn/pull/156)): a second scan
  about five seconds after every start, caused by an old USB transfer note, is gone.

What Reborn does when playing music (Shuffle, Repeat, Play All, Up Next, Previous) is explained in one place:
[overview](https://github.com/SchulzCode/Y2Reborn/issues/118#issuecomment-7022541625).

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: the
  play options shared by every list and their menus, the three lists, plays counted without Last.fm or tags, a bounded
  play history that survives saving, the cover after a restart, the quiet start scans and the transfer note at start.
- **On one player** (test builds 264 and 265, the same version): lyrics from `.lrc` files, timed and plain lyrics in
  FLAC and MP3 tags, a 40-line `.lrc` with Japanese lines, seeking, the screen off and on while lyrics show.
- **Not tested on a player**: the new Music lists, Shuffle All on Songs, the cover after a restart, lyrics over
  Bluetooth or after sleep, and this release itself.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

- AirPods can stay silent after you walk out of range and come back while the link holds; pause and play brings the
  sound back ([Y2Reborn #132](https://github.com/SchulzCode/Y2Reborn/issues/132)).
- Seeking still blanks the cover for a moment ([Y2Reborn #151](https://github.com/SchulzCode/Y2Reborn/issues/151));
  a fix is under review.
- Everything else listed for [Beta 9.3.32](Y2-LINUX-1.0-BETA-9.3.32.md#known-issues) still applies.

## Technical appendix

- Reborn `reborn-ui/src/play_options.rs`: header rows, the group menu (`group_*`, `collection_*`, `pl_*`),
  `Ui::play_choice` and `Ui::play_page`; the three lists are `Screen::Tracks` with the filters `recent:added`,
  `recent:played`, `most:played`. `reborn-core/src/play_stats.rs`: play count and last play by path (sequence number,
  at most 5 000 songs) in `state/play-stats.json`; `scrobble::Counted::play` is `None` for untagged songs.
- Lyrics: `reborn_media::embedded_lyrics` (C `rb_media_lyrics_file`), `lyrics::parse_tag`.
- `TransferRefresh::since` starts at the USB generation on record; `scan_toast` answers asked scans only.
- Images built from Linux `27199eb9`, Reborn `15b6aab`.
