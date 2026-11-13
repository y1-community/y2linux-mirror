# Ideas that won't be built, and why

Thank you for every idea. Some have been asked for more than once and won't be built, for the reasons below. If
yours is on this list, please don't open a new request for it; if you have a new argument or a different way to
do it, comment on the issue linked there instead.

Something missing here may still be a duplicate: please search the open and closed issues of
[Y2Linux](https://github.com/SchulzCode/Y2Linux/issues?q=is%3Aissue) and
[Y2Reborn](https://github.com/SchulzCode/Y2Reborn/issues?q=is%3Aissue) first.

## Battery and charging

| Idea | Why not | Issue |
|---|---|---|
| Battery percentage on the charging screen while the player is off | The Y2 can't measure the charge going in or out, so the percentage is estimated from the battery's voltage. While charging, that voltage reads too high; the running player corrects for it, a switched-off one can't. The number would be wrong most of the time. Switch the player on to see the level. | [Y2Reborn #72](https://github.com/SchulzCode/Y2Reborn/issues/72), [Y2Linux #109](https://github.com/SchulzCode/Y2Linux/issues/109) |
| Stop charging at 80 % or 90 % | For the same reason the percentage isn't precise enough for a charge limit to mean what it says. | [Y2Reborn #64](https://github.com/SchulzCode/Y2Reborn/issues/64) |
| Battery level of Bluetooth headphones | Most headphones report it only over the phone-call side of Bluetooth, which the player doesn't run: a second connection to every pair of headphones makes Bluetooth less steady. Without it only a few would show a level (AirPods never). | [Y2Reborn #95](https://github.com/SchulzCode/Y2Reborn/issues/95) |

## Buttons and wheel

| Idea | Why not | Issue |
|---|---|---|
| Double tap on the centre button | To tell a double tap from a single one, the player would have to wait after every tap, which makes the button slower everywhere. Tap selects, hold opens the menu. | [Y2Reborn #98](https://github.com/SchulzCode/Y2Reborn/issues/98) |
| Wake the player with the centre button or the wheel | Only Power wakes it, so it can't wake in a pocket or bag. What the other buttons do with the screen off is in Settings → Customize → Buttons → Screen Off. | [Y2Linux #71](https://github.com/SchulzCode/Y2Linux/issues/71) |
| A setting for the wheel's seek step | The wheel already seeks faster the faster you turn it. | [Y2Reborn #50](https://github.com/SchulzCode/Y2Reborn/issues/50) |
| A setting for button debounce | If buttons bounce on a player, that gets fixed for everyone rather than turned into a setting. Send an error report if you see doubled presses. | [Y2Reborn #54](https://github.com/SchulzCode/Y2Reborn/issues/54) |

## Menus and screens

| Idea | Why not | Issue |
|---|---|---|
| Reorder the rows of a menu | Every menu lists its actions in the same order, so each action is in the same place wherever you are. (The places on Home can be reordered: Settings → Customize → Home Menu.) | [Y2Linux #66](https://github.com/SchulzCode/Y2Linux/issues/66), [Y2Reborn #98](https://github.com/SchulzCode/Y2Reborn/issues/98) |
| Play state and track time in the standard status bar | The standard bar stays clock, Wi-Fi, Bluetooth and battery. A skin can show both anywhere: see the [skin guide](https://github.com/SchulzCode/Y2Reborn/blob/main/docs/skins.md). | [Y2Linux #69](https://github.com/SchulzCode/Y2Linux/issues/69) |
| More Cover Flow and artist display modes | Different looks are what skins are for; the player itself keeps one. | [Y2Reborn #65](https://github.com/SchulzCode/Y2Reborn/issues/65) |
| Sort options for Audiobooks | One order: in progress first, then new ones A to Z, then finished. | [Y2Reborn #27](https://github.com/SchulzCode/Y2Reborn/issues/27) |

## Sound and playback

| Idea | Why not | Issue |
|---|---|---|
| More volume or gain for Bluetooth, or with an equalizer profile | The player lowers the level just enough that the profile's highest boost can't distort. Louder would mean distortion; a profile with smaller boosts keeps more volume. | [Y2Linux #80](https://github.com/SchulzCode/Y2Linux/issues/80) |
| Crossfade or gapless on a manual skip | A skip should be immediate. Gapless and crossfade apply when one song runs into the next. | [Y2Linux #65](https://github.com/SchulzCode/Y2Linux/issues/65) |

## Wi-Fi

| Idea | Why not | Issue |
|---|---|---|
| WPA3-only Wi-Fi networks | The Y2's Wi-Fi chip is too old for WPA3. A "WPA2/WPA3 mixed" setting on the router works. | [Y2Linux #91](https://github.com/SchulzCode/Y2Linux/issues/91) |
