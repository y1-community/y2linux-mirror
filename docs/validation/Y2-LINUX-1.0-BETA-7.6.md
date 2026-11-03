# Y2Linux 1.0 Beta 7.6 (Baseline 67)

Status: **PUBLISHED as GitHub release `baseline-67`** (2026-10-16). Release `1.0.0-beta.7.6`, Reborn `1.0.0-beta.7.6`,
kernel `6.18.56-y2linux-1.0.0-beta.7.6`, update sequence 6100, Linux `58de7d8`, Reborn `36e6994`, git tag `baseline-67` in
both repositories. Built with `tools/production/release.py` in 118 s: container suite **743 OK**, all seal checks
PASS, no MediaTek firmware in any image. Milestone audit: BETA-7.6 in [roadmap-gap-audit](../planning/roadmap-gap-audit.md).

| Image | SHA-256 |
| --- | --- |
| BOOTIMG | `d4a82b01efad4157a8a7b62b2bcc578bf4ca30113cca4672fed2d673884b0705` |
| Y2ROOT | `b3c6cd25a648487d74a9a07e877042efce03368a1f7cf87269695de73728c629` |
| RECOVERY | `87d6cc71cb8d4c0aa1561bef8dc5fcb3b63fedff910880bd8cafe6d8a6f939bb` |
| Y2DATA (no SSH key) | `434528e3d0cffe27c50a4a9f3735e5f0114c3708263a52d99950a66815c95089` |

## Since Beta 7.5 (Reborn `ca8e7b9`)

| Part | Cause / change |
| --- | --- |
| Update frame rate | Owner: the download update UI "has low fps". Cause: the download and check counted only as a spinner (`Ui::spinning`), which redraws at the calm rate (34-ms frame gap, at most 29 fps); the bar's highlight stepped ≈8 px a frame and jumped back to the start, and the percentage jumped with each platform report (4 a second) while the bar glided. Now `Ui::download_shown` (download or check visible, screen on) joins `gliding()`: full frame rate, but not `responding()`, so no Interactive CPU lease (the download runs at nice 10). |
| Update look | The number counts with the gliding bar; the highlight fades at its ends; the Connecting segment eases. A step tracker (Download · Check · Install) replaces "STEP 1 OF 3 · DOWNLOAD", also on the restart screen; the bar is 10 px with a bright head. |

Host evidence: 489 Reborn tests (new: the visible download glides without the CPU lease and stops with the screen off or
behind another screen; the percentage follows the bar), strict Clippy, fmt; previews `34`–`34d` in Reborn's look, Classic and
Winamp.

## Device checks owed

1. The new screens appear on the update **after** 7.6 (the running version draws its own download): frame rate during the
   download (`rebornctl metrics`), download duration compared with earlier updates.
2. Still open: the 7.5 status bar look, the 7.4 Now Playing battery check, the Songs list scroll sample, 7.1 and 7 checks.

## Known gaps

As Beta 7.5. Under light skins (Classic) the bar's white highlight and the restart screen's glow are remapped into dark
smudges (unchanged since the skins' introduction).
