# Y2Linux 1.0 Beta 9.3.20: audiobooks keep your place, albums play in order, steadier Wi-Fi after waking

Release `1.0.0-beta.9.3.20`, Y2Reborn `1.0.0-beta.9.3.20`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.20`, update
sequence 15400, root `2026.08-platform-v1.252`. Published as GitHub release `baseline-229` (test build 229). Previous
release: [Beta 9.3.19](Y2-LINUX-1.0-BETA-9.3.19.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

A bug-fix release for the open issues. **Audiobooks no longer jump back to the start of a long chapter** when
Previous or Next is pressed by accident (#103), and Now Playing's menu has Previous Chapter / Next Chapter. **A single
album plays in order** even with Shuffle on (Y2Reborn #113). **Skins' Cover Flow titles move with the covers**
(Y2Reborn #112). **Wi-Fi after waking** no longer gives up on a network it can see, and Bluetooth no longer holds the
radio for almost a minute looking for headphones that weren't connected before the sleep (#83). Settings → Library
counts **audiobook files too long to read** (#84), and Diagnostics has **SBC Auto Rate** for anyone hearing
Bluetooth dropouts (#99).

## What changed

- **Audiobook buttons** (#103): Previous and Next go to a chapter start only when it is less than 10 minutes away;
  further away they skip 30 seconds. So one press in a pocket costs half a minute, not an hour of a one-chapter
  book. Previous near the start of a book's file continues into the file before. Hold Select on Now Playing for
  **Previous Chapter** and **Next Chapter**, which go to the chapter start however far away. While a book is
  starting, the buttons do nothing (before, a press then could act on the music queue).
- **Your place at 0:00** (#103): a book you had started kept no place when you stopped at its very beginning, and
  dropped out of "continue". It now keeps it; only Forget Progress removes it.
- **One album, in order** (Y2Reborn #113): when everything you start is one album (same album, album artist and
  folder; "CD 1" / "CD 2" folders count as one), it plays in order whatever the Shuffle setting. Playlists and mixed
  songs still shuffle. Now Playing still shows the Shuffle setting itself.
- **Skins: Cover Flow titles** (Y2Reborn #112): a skin that draws its own Cover Flow titles now sees them glide and
  fade with the covers, like Reborn's own.
- **Wi-Fi after waking** (#83): the player gave up on a network as "not found" while the radio was still joining it
  and restarted Wi-Fi. It now gives up only when the radio isn't joining and the network isn't in the scan. After
  a sleep without Bluetooth headphones, Bluetooth looks for them once on waking (as when you switch it on yourself
  it still looks three times); before, its three searches of about 18 seconds each held up Wi-Fi on the shared
  radio.
- **Long audiobook files** (#84): a single file of more than about 50 hours is too big to read and was missing from
  the library without a word. Settings → Library now counts it with the files that couldn't be read.
- **SBC Auto Rate** (#99): Diagnostics (Settings → System → About, tap Version seven times) has a switch for SBC
  headphones to lower the rate when the signal gets weak. Off by default, as since 9.3.16.
- **Skin guide**: rewritten for skin makers, with three example skins (Ember, Poster, Console).

## How it was tested

- **New tests:** the audiobook buttons (near and far chapters, across files, last chapter), the Now Playing menu,
  keeping a place at 0:00, when a queue counts as one album (same-named albums in different folders do not), the
  skin Cover Flow titles, a Wi-Fi join with the network in the scan, the SBC Auto Rate switch, and the Bluetooth
  wake (one search after a sleep alone, three after switching on, cleared by an interrupted sleep).
- **Against 9.3.19:** every Reborn test that passed on 9.3.19 still passes, and the preview pictures of every screen
  state, in Reborn and in every shipped skin (3,760 files), are identical.
- **Before release:** the platform's full check, Reborn's workspace tests and the tests of the release images.
- **Not yet on a player:** Wi-Fi after waking and the Bluetooth wake need a real sleep; the audiobook buttons need a
  long book.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.19](Y2-LINUX-1.0-BETA-9.3.19.md#known-issues) still applies.

- One audiobook file of about 36 to 50 hours shows up but may not play (the player's index of it doesn't fit);
  longer ones are counted as unreadable. Split such books into parts of up to 30 hours.
- Clear All for favourites (#103) isn't there yet.
- Whether SBC Auto Rate helps against Bluetooth stutter (#99) is not known yet.

## Technical appendix

- Reborn `5c6e769`: `reborn-core::books` `Listening::button_step` (`BUTTON_REACH_MS` 10 min, `BUTTON_SKIP_MS` 30 s,
  backward continues into the previous part), `chapter_step` for the menu, `Shelf::record` keeps a place that
  existed; Now Playing book rows Previous/Next Chapter → `Effect::PlayBook(key, BookStart::Chapter(n))`;
  `queue::one_album` (album tag, album artist, `album_folder` folding CD/Disc folders) makes `play_members` use
  `ShuffleMode::Off`; `Canvas::flow_caption` + `skin/draw.rs::flow_glide`; `reborn-platform::wifi`
  `PendingJoin::absent()` (STATUS not associating/handshaking and SCAN_RESULTS without the SSID) before NotFound or
  the deadline restart; `media.c` checks EFBIG before the demuxer error; `components::left_out_note`;
  `PlatformTask::SbcAbr` → `codec-settings --sbc-abr`. Architecture guard skips every `tests.rs` host module.
- Linux `588c7789`: y2-radiod `links::Trigger::Wake` (limit 1) when `asleep_alone` (no connected audio at quiesce);
  cleared on an interrupted sleep and on `set_power`. Code-quality baseline exception for the SBC plumbing (glue
  lines in files already over the ceilings). Bump `f6ba913`, `d5403d04`.
- F56 (silent cold restarts) has no code change: the PMIC keeps no brown-out record.
