# Y2Linux 1.0 Beta 9.3.29: Buttons

Release `1.0.0-beta.9.3.29`, Y2Reborn `1.0.0-beta.9.3.29`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.29`, update
sequence 16600, root `2026.08-platform-v1.282`. Published as GitHub release `baseline-259` (build 259; test builds
257 and 258 before it). Previous release: [Beta 9.3.28](Y2-LINUX-1.0-BETA-9.3.28.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

**Buttons** (Settings → Customize → Buttons) lets you give any button, any hold and either turn of the wheel the
job you want, separately in the menus, on Now Playing and with the screen off. Changes are kept as layouts;
**Reborn Default**, how the buttons always worked, is always one step away. It is built so nobody can lock
themselves out: Power never changes, a layout that couldn't select, go back or scroll is not saved, a new layout
must be kept with its own buttons or it undoes itself, and the Quick Menu brings Reborn Default back.

## What changed

- **Three places, one drawing each.** In Menus, On Now Playing and Screen Off each show the player. Turn the wheel
  or press a button to pick it; its card says what a press and a hold do. Select changes them. Now Playing uses
  what the menus do for every button you leave alone; the dark offers only what makes sense in a pocket.
- **Layouts.** Your changes become **My Buttons** (up to four). Built in: Reborn Default, Pocket (screen off: only
  Play/Pause and volume), Wheel Seeks (the wheel moves through the song on Now Playing), One Hand (screen off: the
  wheel is the volume, the centre plays and pauses, the side buttons skip). **Hold Time** sets how long a hold is.
- **Safety.** Power is in no layout. The Buttons pages, the Quick Menu and the on-screen keyboard always answer
  as Reborn Default. **Save Changes** checks the layout and walks the player's own menus with it, then asks
  **Keep These Buttons?**: unless Keep is chosen with the new buttons, the old ones are back after 15 seconds. While
  another layout is in use, the Quick Menu has a **Buttons** tile (**Use Default**). Back held for five seconds in
  the first minute after start also brings Reborn Default back.
- **A buzz in the dark.** With Haptics on, a button that did something with the screen off buzzes once.
- **The older settings** "Wheel on Now Playing" and "Buttons While Screen Off" are part of Buttons now; a player
  that had changed them got that choice as a layout on its first start.

Guide: **[Buttons](../BUTTONS.md)**.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. All earlier
  tests of the button handling pass unchanged with Reborn Default. New tests: layouts and their check, the walk
  through the menus with built-in and moved layouts, Keep, Undo and the timeout, the Quick Menu tile, the start-up
  escape, commands in the dark, older settings taken over, layouts written by a newer Reborn.
- **On one player** (test builds 257 and 258, then this build), with real button presses fed into its input
  devices: the older settings became Reborn Default; Next pressed on the drawing picked Next; Next changed to
  Shuffle, saved and left alone came back undone after 15 seconds; saved again and kept, the Next button toggled
  Shuffle; Centre set to Shuffle for the dark toggled it with the screen off, Back did nothing there and Power
  woke the player; Power held opened the Quick Menu, whose Buttons tile (its place fixed in 258) brought Reborn
  Default back. The player was left on Reborn Default.
- **Not tested on the player**: holding the five front buttons (also the start-up escape), because they are read
  by polling and an injected press is released at once; the buzz in the dark. Both are covered by the tests.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.28](Y2-LINUX-1.0-BETA-9.3.28.md#known-issues) still applies. New:

- With the player asleep, only Power works; the front buttons can't wake it as they are wired up today.
- Double presses, sharing layouts as files and headphone buttons are not part of Buttons.

## Technical appendix

- Reborn `crates/reborn-core/src/buttons.rs` (layouts, layers, commands, presets, check, migration; stored in
  `Settings.buttons`, additive), `crates/reborn-platform/src/input.rs` (`ActionRouter` answers through the layout
  of the layer a button went down in; protected pages use Reborn Default; Power and the keyboard unchanged; holds
  and repeats reported for every button, hold time adjustable, Power fixed at 650 ms),
  `crates/reborn-ui/src/buttons_page.rs` and `views_buttons.rs` (pages, save walk, trial, the drawing),
  `app/reborn/src/buttons.rs` (trial timeout). Plan and as built:
  [REBORN-BUTTON-MAPPER.md](https://github.com/SchulzCode/Y2Reborn/blob/main/docs/ui/REBORN-BUTTON-MAPPER.md).
- Scope note: [roadmap gap audit](../planning/roadmap-gap-audit.md) (no kernel, platform or image change).
- Images built from Linux `c5571186`, Reborn `0c74ffd`.
