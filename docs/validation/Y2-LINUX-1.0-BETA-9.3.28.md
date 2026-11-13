# Y2Linux 1.0 Beta 9.3.28: a fast Audiobooks list, films that keep their picture

Release `1.0.0-beta.9.3.28`, Y2Reborn `1.0.0-beta.9.3.28`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.28`, update
sequence 16510, root `2026.08-platform-v1.279`. Published as GitHub release `baseline-256` (build 256; build 255 was
an unpublished first build of this version). Previous release: [Beta 9.3.27](Y2-LINUX-1.0-BETA-9.3.27.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

Three fixes. **Audiobooks** with a few hundred books made of many small files was so slow that reaching a letter took
minutes ([Y2Reborn #27](https://github.com/SchulzCode/Y2Reborn/issues/27)); the list is now prepared once and scrolls
fast. A **film's picture could freeze after seeking** back while it was still catching up from the seek before
([Y2Reborn #129](https://github.com/SchulzCode/Y2Reborn/issues/129)). And, found while testing that: **films with few
reference pictures** (as phones and screen recorders often make them) **stopped after their first pictures** on the
video chip.

## What changed

- **Audiobooks.** The list of books (and its Cover Flow) used to be worked out again several times for every picture
  drawn and for every turn of the wheel. It is now kept until the library, a book's progress or the playing book
  changes; only the playing book's "Chapter … · …%" line follows the sound. A fast turn of the wheel moves several
  books at a time, as in Songs.
- **Films after seeking.** After a seek, the picture decodes forward from the nearest key picture until it reaches the
  sound. A second seek during that catch-up (above all back) was ignored, and the picture stood still until the sound
  reached it again. Every seek is now followed.
- **Films on the video chip.** The chip's picture memory was sized from the film's reference pictures alone, but the
  decoder works on several pictures at once. A film with one reference picture got four buffers, ran out after three
  pictures and showed a still picture while the sound played. The memory now counts the pictures in flight.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: the
  Audiobooks list is kept between frames while the playing book's place still moves, a fast turn moves several books;
  the rule that follows a seek during a catch-up.
- **On the computer**: a list of 200 books made of 10 000 files took 1.5 ms per redraw and 2.8 ms per wheel step
  before, 0.02 ms for both after (the player is roughly ten to twenty times slower).
- **On one player** (builds 255 and 256, installed over the air): 200 test books of 50 files each (10 337 tracks):
  Audiobooks opened in Cover Flow with 205 books, 40 wheel steps at 14 ms per frame on average. A 3-minute test film
  with one key picture per minute and a clock in the picture: on build 255 it stood still at its third picture
  ("all 4 frames are in use"), which led to the third fix; on build 256 it played, a seek to 1:50 followed half a
  second later by a seek back to 0:20 showed 0:22 two seconds later, and a seek to 2:30 showed 2:35 five seconds later.
  The 1080p test film still plays on the chip. The test files were removed again.
- **Not tested**: the reporters' own libraries and films.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.27](Y2-LINUX-1.0-BETA-9.3.27.md#known-issues) still applies.

## Technical appendix

- Reborn `crates/reborn-ui/src/views_books.rs` (`with_book_rows`: rows cached by library stamp, shelf, chapter count
  and playing book; the playing row's status refreshed on each read; `with_books` no longer clones every book to find
  one), `rows.rs` (row count, row and window read from the cache), `actions.rs` (Audiobooks accelerates like the
  library and the time zones).
- Reborn `app/reborn/src/video.rs`: `Shared::moves` counts the sound's moves; `follow_move` seeks again during a
  catch-up when the sound moved since that seek (no loop on a demuxer that lands after the target).
- `buildroot/patches/ffmpeg/0001-y2-vdec-h264-hwaccel.patch`: `ff_y2vdec_open` adds `thread_count` frames under frame
  threading, as FFmpeg's own hardware frame pools do (driver maximum 24 unchanged).
- Images built from Linux `744c913e`, Reborn `dbe6c29`.
