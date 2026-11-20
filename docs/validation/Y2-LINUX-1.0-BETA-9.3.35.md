# Y2Linux 1.0 Beta 9.3.35: Reborn Light, What's New on the player, cover colours and a book for audiobooks

Release `1.0.0-beta.9.3.35`, Y2Reborn `1.0.0-beta.9.3.35`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.35`, update
sequence 17300, root `2026.08-platform-v1.291`. Published as GitHub release `baseline-268` (build 268). Previous
release: [Beta 9.3.34](Y2-LINUX-1.0-BETA-9.3.34.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

Four small requests from the issue list: a light version of Reborn's own look, the release notes on the player
itself, cover colours that look like the cover (jonasevcik's work), and a book instead of a music note for
audiobooks without a cover.

## What changed

- **Reborn Light** ([Y2Linux #112](https://github.com/SchulzCode/Y2Linux/issues/112)): Settings → Skin → Reborn Light.
  It is Reborn itself, drawn exactly as in the dark look and recoloured: so every screen has it, with the same layout
  and motion, and anything added later has it too. Covers, thumbnails and films keep their own colours. Text on a
  coloured button stays white.
- **What's New** ([Y2Linux #128](https://github.com/SchulzCode/Y2Linux/issues/128)): Settings → System → Software
  Update → What's New. The player shows the part of this page's release text written for listeners (not the install
  steps or file lists), fetched once over Wi-Fi and then kept on the player, read with the wheel. **Show What's New
  After Updates** (off unless you turn it on) opens it by itself once after each update, on Home or Now Playing;
  anywhere else a short message says where it is.
- **Cover colours** ([Y2Reborn #161](https://github.com/SchulzCode/Y2Reborn/issues/161), by jonasevcik): the cover is
  reduced to 16 colour areas; the largest real colour wins and keeps its own shade, made just light enough to read.
  Dark covers and covers on white paper no longer fall back to plain white, and muted covers no longer turn orange.
- **A book for audiobooks** ([Y2Reborn #162](https://github.com/SchulzCode/Y2Reborn/issues/162)): an audiobook without
  a cover shows a book, in the lists, on its page, on Now Playing and on Home. Skins can use the icon as `book`.
- **A missing arrow**: the player's fonts have no "→", so "Settings → Customize → Browsing" in the FM Radio message
  showed a gap. Menu paths there (and in What's New) use "›".

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: the
  light colours (every theme colour reads 4.5:1 on the light background; covers and thumbnails are not recoloured),
  What's New (the player's part of a release text, the release that belongs to a version, fetching once and again
  only after a failure, reading within the notes, off by default), the book icon, and the cover colour (dark ground
  with small lettering, white paper with a small coloured area, a muted brown that stays brown, grey and empty
  pictures, contrast for every hue).
- **Previews**: Reborn Light on about twenty screens (Home, lists, album and book pages, Now Playing, menus,
  dialogs, the keyboard, the Quick Menu, Cover Flow, Settings and the skin picker).
- **Not tested on a player**: all four changes, and this release itself.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

- Everything listed for [Beta 9.3.34](Y2-LINUX-1.0-BETA-9.3.34.md#known-issues) still applies.

## Technical appendix

- Reborn Light: theme id `reborn-light` (`reborn_ui::theme::REBORN_LIGHT`), no skin folder; `reborn_ui::light::apply`
  recolours `Tex::Solid/Font/Mask/Glyphs` quads of each finished frame (tokens to light counterparts, translucent
  white to black at 70 %, near-neutral or dark colours with their lightness turned around, bright colours darkened to
  4.5:1, translucent black at 40 %). `color::ON_ACCENT` for type on accent fills. `reborn-preview --light`.
- What's New: `reborn_core::whats_new` (`release_title`, `lines`), `reborn_platform::whats_new::fetch` (GitHub
  releases API through the bounded `radio::fetch`), app `runtime/whats_new.rs` (cache `state/whats-new.json`),
  `Screen::WhatsNew`, settings `whats_new_after_update` and `whats_new_seen`.
- Cover colour: `reborn-ui/src/accent.rs` (median cut to 16 swatches, counting sort; jonasevcik's commit kept with
  its author). Book icon: `tools/assets/build_ui_assets.py` `ic_book`, `components::placeholder_of`.
- Images built from Linux `f3b7a8f7`, Reborn `18d82b6`.
