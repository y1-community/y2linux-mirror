# POWER-FINAL-34 — all skins shipped, battery level fixed

Status: **SEALED, BASELINE 34** (2026-10-14; git tag `baseline-34`; awaiting the owner flash). Package
`out/y2linux-platform-v1-power-final-candidate-34/` (fallback = 33), receipts `out/deep-sleep-final/build34-receipts/`.
BOOTIMG `ee80a82cbc71afbdd80aceaa178bb76b22c757c85ebd7b90daef0b17fb84c30c`, Y2ROOT
`9d3e93c13fe400834fbf098109b6c4c76383bcad184fc71fa4196600d3e9ff6c`. Identity: build `Y2LINUX-PLATFORM-V1-POWER-FINAL-34`,
kernel `6.18.56-y2linux-platform-v1-power-final-34`, root `2026.08-platform-v1.64`, release
`1.0.0-platform-v1-power-final-candidate.34`; Linux `de62fa9`, Reborn `5b69ef5`. Contains 27–33; kernel and platform
unchanged from 33 apart from the identity.

## What 34 changes

| Area | Change |
| --- | --- |
| Battery level | Reborn shows the level again for the platform's `stock_oam` source (hidden since 29: empty battery in Classic, no number in Reborn's look). Reborn `116f942`. |
| Skins | 15 shipped in `/usr/share/reborn/skins`: Classic, Nano Glass, Walkman, Terminal, Winamp, Paper, Big Type, Cassette, Neon Night, Zune, Mini Silver/Pink/Blue/Green/Gold (3.1 MB, OFL fonts, licence texts in legal-info). |
| Skin engine | Font `charset: ascii`, text `case: lower` (Zune). |

## Tests

Reborn 398 tests, clippy clean. On 33, 13 of the skins (as trial copies in `Themes`) loaded on the device in 87–119 ms
each; Zune needs the 34 engine (host preview only). Platform: `validate.py` PASS, ELF closure 354 PASS, installed ARM
PASS (40 modules), privacy clean, inventory 108 packages, container suite **715 OK**.

## Owner checks after the flash

1. The battery shows a number in Reborn's look and a filled battery in Classic.
2. Settings → Display → Theme lists 15 skins (the trial copies in Internal Storage `Themes/` are hidden by the shipped
   ones and can be deleted); try Zune.
