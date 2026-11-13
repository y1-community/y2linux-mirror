# Y2Linux 1.0 Beta 9.3.30: films in every skin, tidier albums, Shuffle in folders

Release `1.0.0-beta.9.3.30`, Y2Reborn `1.0.0-beta.9.3.30`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.30`, update
sequence 16710, root `2026.08-platform-v1.284`. Published as GitHub release `baseline-261` (build 261; build 260 before
it on one player). Previous release: [Beta 9.3.29](Y2-LINUX-1.0-BETA-9.3.29.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

Films now play with any skin ([Y2Reborn #131](https://github.com/SchulzCode/Y2Reborn/issues/131)); before, a skin
with its own Now Playing showed a film like a song. A Reddit user's library of "over 720 albums" led to three
fixes in how songs become albums, a **Shuffle** row on every opened folder, and a headphone profile for the
Truthear x Crinacle Zero:BLUE2 they asked about.

## What changed

- **Films in every skin.** A film on Now Playing is always the picture on black, whatever the skin, and the
  skin's colours no longer tint it. Songs keep the skin's Now Playing.
- **Albums.** Within one folder, the songs of one album title now always make one album: when they name
  different Album Artists, or none while different artists sing, it is a **Various Artists** album (before, it
  was split into one album per name). Two different albums of the same title without the Album Artist tag stay
  apart by artist (before, they were merged), and songs without an album title are listed under their own artist
  (before, they formed one mix). The music files are not changed, and Last.fm still gets each song's own tag.
- **Shuffle in folders.** An opened folder in Music → Folders starts with **Shuffle**: everything in the
  folder, its folders included.
- **Headphone profile** for the Truthear x Crinacle Zero:BLUE2 (AutoEq, measured by Kazi): 48 profiles in all.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: a
  film in the Classic skin (picture drawn and untinted; it failed before the fix), albums with differing,
  missing and mixed Album Artist tags and songs without an album title, a folder's Shuffle with its subfolders.
- **On one player** (builds 260 and 261, over the air): a film played full screen in the Classic skin; an opened
  folder showed Shuffle first (on 260 with a folder's icon, corrected in 261). The player went back to the
  Reborn skin.
- **Not tested on the player**: the album changes with a large library (the owner's library is small and tidy),
  the BLUE2 profile by ear.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.29](Y2-LINUX-1.0-BETA-9.3.29.md#known-issues) still applies.

## Technical appendix

- Reborn `crates/reborn-ui/src/screens.rs` (`film_playing`: no skin layout and no palette remap for a film),
  `skin/draw.rs` (`remap` leaves `Tex::Video` alone); `crates/reborn-core/src/albums.rs`
  (`unify_album_artists`: one name → it, several → Various Artists, none → the one singing artist else Various
  Artists, untitled songs → own artist; `own_album_artist` keeps the tag); `catalog.rs` `folder_rows` (Shuffle
  row), `selects.rs` (`play_filtered` takes a folder's `group_members`); `assets/eq` (+1 profile).
- Images built from Linux `d26d82b8`, Reborn `be78d4a`.
