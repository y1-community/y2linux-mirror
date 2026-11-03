# POWER-FINAL-48 — download screen with progress, speed and time left

Status: **PUBLISHED** as GitHub release `baseline-48` (2026-10-16, owner: "publish 48"). Package `out/y2linux-platform-v1-power-final-candidate-48/` (fallback =
47), receipts `out/deep-sleep-final/build48-receipts/`. BOOTIMG
`5a1fa90e26a9a890ed6c2bca0fb201ccfa26920ea11936460fbfdb26fba8d74b`, Y2ROOT
`1d7c4bcae8b4bae1e2068beeb54f1960a271accb07ffc73ca3c5649ba23ae047`, RECOVERY
`6c14a2f79f055ec2a18101474804141a7b6ec07330f75771fa2a7cb730370a3f`. Kernel `6.18.56-y2linux-platform-v1-power-final-48`,
update sequence 4800, Linux `d5cc381`, Reborn `62af097`. Container suite **718 OK**, host update tests **24 OK**,
Reborn fmt/clippy/tests and GLES comparison (81 states) OK, all seal checks PASS. Signed with the release key:
`…-48-ota-release/`. Contains 47 (DSI DCM off), whose device test is still open.

## Why

Owner after downloading 47 over Wi-Fi: the animation stuttered, the screen showed no progress and nothing said that
the download was done; wanted progress and download speed. Found: Reborn skips the platform refresh while any
platform task runs, so the percentage never moved; the stage reply showed nothing; after the 18 s download (58.7 MB,
≈3.3 MB/s) the player decoded the package twice (`check`, then `queue`) for 24 s with nothing new on screen.

| Change | Detail |
| --- | --- |
| Platform | Live progress in `/run/y2/update-progress.json` (RAM, ≤4/s: phase, bytes, speed over 4 s, seconds left); the queue's decoding check writes `/run/y2/update-check-progress`. One decoding pass instead of two (about 12 s less; the queue still refuses a wrong-slot boot image). Download and check at nice 10. No per-MiB journal writes on Y2DATA. |
| Reborn | Reads both files five times a second while a download runs. Screen: *Step 1 of 2 · Download* / *Step 2 of 2 · Check*, large percentage, a bar that glides between reports with a moving highlight, MB of MB and speed, time left (in fives, then minutes); *Connecting…* with a sweeping segment before the first report. At the end the page turns to **Ready to Install** at once and a notice says *Update downloaded. Ready to install.* wherever the owner is; a failed download says nothing was changed. |

Previews: Reborn `reborn-preview` states `34-update-downloading`, `34b-update-download-progress`,
`34c-update-download-check`.

## Owner steps

1. First 47's test (installed from the queue, five recovery probes).
2. Install 48 over Wi-Fi. That download still uses 47's screen; 48's screen shows from the next
   download on (or a development reinstall published for the test).

## Device results

- **Installed over Wi-Fi from the release** (47 before it, also over Wi-Fi): boot `7ab5099e`, `Acknowledged`, recovery
  screen lit, install 53 s (archive `20261016/151`). The new download screen shows from the next download.
- Five restart-entered recovery probes: all lit; details in [47](Y2-PLATFORM-V1-POWER-FINAL-47.md#device-results-on-48-archive-20261016151).
