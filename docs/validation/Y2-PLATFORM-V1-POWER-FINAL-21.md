# POWER-FINAL-21 — quiet display power transitions (no colour flash, no white screen at power-off)

Status: **INSTALLED; shutdown fixed, startup flash remained → 22** (2026-10-13). Package `out/y2linux-platform-v1-power-final-candidate-21/`
(fallback = 20), receipts `out/deep-sleep-final/build21-receipts/`. BOOTIMG
`ea1da0b7bccf20a846839288d811205431cfbac6f441bc7107bf74a5b542908e`, Y2ROOT
`9a9b99ce9dbc4768485ac1c7446caef6be2e69cfb29e6e0a1a1271e075a17b77`. Identity: build `Y2LINUX-PLATFORM-V1-POWER-FINAL-21`,
kernel `6.18.56-y2linux-platform-v1-power-final-21`, root `2026.08-platform-v1.51`, release
`1.0.0-platform-v1-power-final-candidate.21`; Linux `0b3e0e1` on `main`, Reborn unchanged (`217e28d`). Flash BOOTIMG +
ANDROID only with `MT6582_preserve_data_scatter.txt`, Download Only. Everything of 20 (sleep) is unchanged. Scope
audit: "POWER-FINAL-21 quiet display power transitions scope audit" in the
[roadmap gap audit](../planning/roadmap-gap-audit.md).

## Owner report

The startup sometimes flashes colours; after the shutdown animation the panel turns white or shows colours.

## Causes found (source and log analysis)

- **Shutdown:** the GC9503V panel driver linked the backlight to DRM (`drm_panel_of_backlight`). Every pipeline enable
  then called `backlight_enable()`, which resets `bl_power` to on. Reborn ends its shutdown on a black frame with the
  light off and is then stopped; when its DRM file closes, the core restores the stale fbdev buffer and enables the
  pipeline again — lighting the panel over white or console/colour content. The platform's own `bl_power=4` after
  the kill can land before that close (a race, hence "sometimes").
- **Startup:** the splash's first `drmModeSetCrtc` fully re-initialises the DSI link (`modeset: PHY/clocks ready,
  initialize DSI`, ≈0.48 s) while the bootloader's picture is still lit; a panel losing its link with the light on
  shows colour noise.

## What 21 changes

1. `panel.c`: no `drm_panel_of_backlight()`. The backlight is owned only by its explicit writers: Reborn (`bl_power`
   at screen sleep/wake and shutdown), the platform power service, and the offline charger (`brightness`).
2. `backlight.c`: `.shutdown` switches all four ISINKs off (PMIC 0x356[3:0]) during `device_shutdown`.
3. `reborn-splash.c`: `bl_power=4` immediately before its `SetCrtc`, `bl_power=0` after two vblanks, unconditionally
   (a failed modeset never stays dark). The backlight device exists at 0.27 s, the splash starts at 0.49 s; the
   initramfs carries the new splash. Expect a very short dark moment between the logo and the splash.

Not changeable: a flash during the bootloader's (LK) own panel initialisation, before Linux starts.

## Tests

`test_display_quiet_power` (no DRM backlight link, sinks off at shutdown, splash modeset dark and always relit) plus
the existing splash, backlight and idle suites. Locked-container production suite: 692 cases, exit 0. Package
validation, ELF, installed ARM, privacy and inventory PASS.

## Hardware result (owner, boot `a0c4d60e`, archive 118)

The shutdown animation now ends dark (fixed). The colour flash between the logo and the splash remained: the DSI
hand-over parks LK's stream at 0.489 s and Linux's first modeset streams at 0.532 s, before the splash runs; see the
[22 report](Y2-PLATFORM-V1-POWER-FINAL-22.md). The first USB connection after this flash was again briefly flaky
(SSH timeout, one slow ping), recovering by itself within seconds.

## What to check after the flash

1. Power on a few times: no colour flash (a brief dark moment after the logo is expected).
2. Turn off a few times: the shutdown animation ends dark and stays dark.
3. Screen off/on and sleep/wake: the screen still comes back lit (Reborn switches the light explicitly).
4. Then the 20 sleep checks on 21: `clocks.unused_handoff` UART1 `never_initialised`, a ~35-min sleep (`last_lost`),
   the overnight drain.
