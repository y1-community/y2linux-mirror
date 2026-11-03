# Y2Linux 1.0 Beta 7.2 (Baseline 63)

Status: **PUBLISHED as GitHub release `baseline-63`** (2026-10-16). Release `1.0.0-beta.7.2`, Reborn `1.0.0-beta.7.2`,
kernel `6.18.56-y2linux-1.0.0-beta.7.2`, update sequence 5900, Linux `78b548b`, Reborn `846da2d`, git tag `baseline-63` in
both repositories. Built with `tools/production/release.py` in 122 s: container suite **743 OK**, all seal checks
PASS, no MediaTek firmware in any image. Milestone audit: BETA-7.2 in [roadmap-gap-audit](../planning/roadmap-gap-audit.md).

| Image | SHA-256 |
| --- | --- |
| BOOTIMG | `498a26488000935b7812a61121cc809158e92aa07f25624805a64b89eb0fa865` |
| Y2ROOT | `ac93c8ce42a83e2c1421ff7f2b4770034483639ce4c3b8541c7fff93c8e0f31d` |
| RECOVERY | `996f62eafbc662bbc0b1b40a763a5c144712dbe8e7a16dfdbe77ca3bf97671ee` |
| Y2DATA (no SSH key) | `4cf9d40d85717e8bdf7a4a380f83ee3671d6b6c0baa1765040f34c7a57a11873` |

## Since Beta 7.1 (Reborn)

Owner (2026-10-16): more animation to make the UI feel polished and responsive, at 60 fps; the Now Playing progress as a
"cool" wave, not a perfect sine. Reborn `634f83a`.

| Part | Detail |
| --- | --- |
| Motion system | `crates/reborn-ui/src/motion.rs`: per-key eased values (exponential, ≈125–190 ms), retargetable at any moment, frame-rate independent, settled values stop all redrawing. Anything moving keeps Cover Flow's 60-fps path (12-ms frame gap, Interactive CPU lease). Off in previews and tests (`Ui::ui_motion`); `reborn-preview --motion` captures frames mid-animation. |
| Lists and focus | Shared components mark moving quads (`canvas::Moving`): the focus row and Home's marker glide; Artists, Songs, Genres, search, Up Next, Folders, Playlists and the radio lists scroll smoothly (one row more on each side, hidden at rest; the status bar stays above); a long wheel jump glides only its last row. |
| Screens, menus, messages | New screens slide in from the right, Back slides out (200 ms, ease-out); sheets rise over a fading scrim; dialogs and toasts fade in; switches slide their knob and blend their colour. |
| Now Playing | The position moves on between reports; the progress is a travelling wave (two sines, breathing height, tapered ends, opaque 2.6-px strokes with a faint 5-px edge) that lies flat when paused; a new song fades in; the play button dips on a change. Continuous 60 fps while playing and visible. |

Host evidence: 487 Reborn tests (new: motion springs, wave while playing / flat when paused, focus glide, slide with one focus
target, long jumps), strict Clippy, fmt; mid-animation captures compared by eye. Device frame rate not yet measured.

## Device frame rate (measured on Beta 7.3, 2026-10-16)

Sampled with the cable in (Beta 7.3 removed the covering computer screen) by polling `rebornctl metrics` and `status`
every 0.5 s for 4 minutes while the owner navigated (archive `out/deep-sleep-final/evidence/20261016/157-beta73-fps-sample`,
`fps2.jsonl`, sampler `fps-sampler.py`):

| Situation | Result |
| --- | --- |
| Motion (113 busy half-seconds: focus glide, slides, menus, playlists) | median **62 fps**, up to 66 (the panel's ≈64 Hz); frames vsync-paced at ≈14.7 ms; **0 missed frames** |
| Now Playing while playing (wave), 22 s | steady **64–65 fps**, every half-second |
| Continuous wheel on Home, 6 s | steady **64 fps** (≈8 ms CPU per frame) |
| At rest (Home, Now Playing paused) | **0 fps**: nothing is redrawn |
| Isolated calm-rate frames on Home | up to 23 ms CPU, at most a few per second, never during motion |

Not yet sampled: smooth scrolling in the Songs list (the owner's path did not pass it); battery while Now Playing plays with
the screen on.

## Device checks owed (after the owner's over-the-air install)

1. Frame rate: measured (above); the Songs list scroll is still to be sampled.
2. The wave travels while playing and lies flat on pause; fast wheel scrolling stays immediate.
3. Battery: Now Playing visible while playing redraws continuously; check the CPU lease ends when the screen is off.
4. Still open: Beta 7.1 checks (USB screen, update answer, automatic time zone), Beta 7 checks.

## Known gaps

FM radio (Beta 8); no 12/24-hour switch; sleep ≈7.8–9.4 mA vs stock ≈1.3 mA; Bluetooth range hardware-limited; no
headphone-jack detection.
