# Y2Linux 1.0 Beta 9.3.25: the Y2 plays videos (early version)

Release `1.0.0-beta.9.3.25`, Y2Reborn `1.0.0-beta.9.3.25`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.25`, update
sequence 16200, root `2026.08-platform-v1.271`. Published as GitHub release `baseline-248` (build 248). Previous
release: [Beta 9.3.24](Y2-LINUX-1.0-BETA-9.3.24.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

**The Y2 plays films.** Video files copied onto Internal Storage or the SD card appear under **Videos** on Home
and play on Now Playing, with subtitles. H.264 and Xvid/DivX films are decoded by the player's own video chip,
through a driver written for this project. **This is an early version**: it was built and tried on one player in a
few days, parts of it are untested, and bugs are to be expected. Also new: a song tagged "Pop/Rock" is in both
genres ([Y2Reborn #124](https://github.com/SchulzCode/Y2Reborn/issues/124)).

## What changed

- **Videos.** Copy films onto Internal Storage or the SD card like songs; any folder works. After the next scan
  **Videos** stands on Home with a picture of each film. A film plays on Now Playing: the picture fills the
  screen, a button press shows title and time line for three seconds, Play/Pause, Previous/Next, the wheel and the
  volume work as for a song, and the sound goes to whatever output is chosen. A film goes on where it was left,
  and the screen stays on while it plays. Films never appear among songs, albums, artists, genres, folders,
  library shuffles or on Last.fm.
- **Subtitles** from a text track in the file, or from a file with the film's name beside it (`.srt`, `.ass`,
  `.ssa`, `.vtt`).
- **Genres joined with "/"** are split, as genres joined with ";" already were. "Singer/Songwriter" stays whole.

Everything about formats, converting and what was measured is on one page: **[Videos on the Y2](../VIDEO.md)**.

## Which files to use

The screen has 480×360 pixels. Every picture is shrunk to it, so a large film looks no better than a small one;
it only takes more storage and more battery.

| | |
|---|---|
| Best | MP4, H.264 video (8 bit), AAC sound, 640×360 (480×360 for 4:3 films), up to 30 pictures a second, about 1 Mbit/s: roughly 8 MB a minute |
| Plays on the video chip, up to 1920×1080 | H.264 (Baseline, Main, High; interlaced too), MPEG-4 Part 2 (Xvid, DivX 4/5), in `.mp4` `.m4v` `.mov` `.mkv` `.avi` |
| Plays on the processor, smooth only when small | VP8, VP9 (`.webm`, `.mkv`), DivX 3, H.263, interlaced Xvid/DivX, 10-bit H.264 |
| Does not play: convert first | H.265/HEVC, AV1, DTS sound, picture subtitles (DVD, Blu-ray), a file with pictures and no sound |

Converting with FFmpeg:

```
ffmpeg -i film.mkv -vf "scale=-2:360" -r 30 -c:v libx264 -profile:v high -pix_fmt yuv420p -crf 23 -c:a aac -b:a 160k film.mp4
```

On a card formatted as FAT32 one file cannot be larger than 4 GB.

## What was measured

On one player, on the cable, with test films (patterns, a moving fractal, noise), ten seconds each:

| Film | Decoded by | Pictures dropped | Whole player, share of one of four cores |
|---|---|---|---|
| H.264 1280×720, 30 a second | video chip | 0 | 61 to 63 % |
| H.264 1920×1080, 24 a second | video chip | 0 | 64 % |
| Xvid 1280×720, 25 a second | video chip | 0 | 57 to 58 % |
| MPEG-4 720×576, 25 a second | video chip | 0 | 59 % |
| VP9 640×360, 25 a second | processor | 0 | 80 % |

The video chip needs about 4 ms for a 1280×720 picture and 9 to 12 ms for 1920×1080. Most of the remaining work is
shrinking the picture to the screen.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed.
- **On one player**: this release was installed over the air. Pictures from the video chip were compared with
  FFmpeg's software decoder on the player: H.264 is identical (130 of 135 standard conformance streams, nine
  films; the five others use rare stream features and are listed in the decoder document); Xvid/DivX pictures
  differ by at most 5 of 255 steps over 200 to 499 pictures of ten films, which the format allows because each
  decoder may use its own arithmetic. Films were played in Reborn and checked by screen captures: the list,
  thumbnails, the film, the paused view, seeking, resuming, subtitles.
- **Not tested**: sleep and wake with a film open; battery life with films; lip sync judged by a person; damaged
  files; very long films; a player with the second screen type; real films of many kinds.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.24](Y2-LINUX-1.0-BETA-9.3.24.md#known-issues) still applies. New, all about video:

- Sleep and wake with a film open are untested.
- Over Bluetooth the sound is later than the picture by the headphones' own delay.
- The first text subtitle and the default sound track are used; there is no choice and no switch.
- Skins draw their own lists; the film always uses Reborn's own screen.
- Interlaced Xvid/DivX and 10-bit H.264 are decoded by the processor and may stutter above DVD size.
- A picture that is cropped at the top or left inside an H.264 file is shown uncropped.

## Technical appendix

- Kernel: `kernel/platform/vdec.c`, `vdec-h264.c`, `vdec-mpeg4.c`, interface `y2-vdec.h` (`/dev/y2-vdec`: size
  the buffers, map them, one call per picture). Power domain VDE through the SPM provider, off 200 ms after the
  last picture, on close and before every sleep. No MediaTek library or firmware; the register sequences were read
  out of the stock firmware's open user-space library and are written down in
  [video-decoder.md](../knowledge/video-decoder.md). Root-only bring-up files stay in debugfs
  (`/sys/kernel/debug/y2_vdec`).
- FFmpeg 9.0.2: `buildroot/patches/ffmpeg` adds the hardware paths `h264_y2vdec` and `mpeg4_y2vdec`; software
  decoders h264, mpeg4, h263, msmpeg4v3, vp8, vp9; ac3; Matroska and AVI demuxers; text subtitle decoders.
- Reborn: `crates/reborn-media/native/video.c` (second reader, NEON shrink and colour to at most 480×360),
  `app/reborn/src/video.rs` (reader thread, resume in `state/videos.json`), `crates/reborn-ui/src/views_videos.rs`.
  A film is a track whose codec names a picture and a sound stream.
- MPEG-4 specifics: the picture is taken from motion compensation, not from the stock path's deblocked copy;
  video packet headers are cut out of B pictures because the hardware misses one in front of a skipped
  macroblock. Interlaced streams, global motion compensation and data partitioning go to software.
- Plan and decisions: [VIDEO-PLAYBACK-PLAN.md](../planning/VIDEO-PLAYBACK-PLAN.md); audit entry VIDEO and its
  MPEG-4 addendum in the roadmap audit. Images built from Linux `d163c3e5`, Reborn `a5d85be`.
