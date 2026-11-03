# Y2Linux 1.0 Beta 7.8 (Baseline 71)

Status: **PUBLISHED as GitHub release `baseline-71`** (2026-10-16). Release `1.0.0-beta.7.8`, Reborn `1.0.0-beta.7.8`,
kernel `6.18.56-y2linux-1.0.0-beta.7.8`, update sequence 6200, Linux `21d5ba0`, Reborn `35a3d56`, git tag `baseline-71` in
both repositories. Built with `tools/production/release.py` in 122 s on top of Baseline 70: container suite **745 OK**, all
seal checks PASS, no MediaTek firmware in any image, Last.fm key file in the image as root 0600. Fixes for Beta 7.7's
scrobbling found on the owner's player (planned as "7.7.1"; the release tool takes two numbers after "beta"). Milestone
audit BETA-7.8 in [roadmap-gap-audit](../planning/roadmap-gap-audit.md).

| Image | SHA-256 |
| --- | --- |
| BOOTIMG | `a00e3cbdd179c1bc06939f1b0296794e67c1256cd221d08c1f5c01b542f99bc1` |
| Y2ROOT | `e16cff1d32a4afc39eb33723b0f4c4919a0acba156568c9989a1672c003ccb4b` |
| RECOVERY | `ed2c1b580ca88d179db135b6c75e18bf6aadeb571ffbb72083f9731c6ea97bd0` |
| Y2DATA (no SSH key) | `a5eea5ab0367be89f244fcc22d164c8beb55a25671a2fa3c975c03dd71d3bf24` |

## Device result of Beta 7.7 (owner's player, read-only checks)

The over-the-air update was acknowledged; the QR sign-in worked (session file 0600 on Y2DATA) and the first real scrobble
appeared on last.fm with the right artist, title, album and start time. Two gaps: Now Playing vanished after a pause (it
was sent once per song and Last.fm drops it after the song's length), and nothing about Last.fm reached the log.

## Since Beta 7.7 (Reborn `f117586`)

| Part | Change |
| --- | --- |
| Log | Radio, clock, platform, Last.fm and scrobble events were silently dropped by the log's subsystem list (radio, clock and platform since earlier betas). They are recorded again; a test fails when code logs to a subsystem the list does not know (`e417ecb`). |
| Now Playing | Sent again when playback continues after a pause, when the same song starts over (repeat, Previous) and once more after a failed attempt, at most every 30 s per song (`8bd1452`). |
| Sending | A play Last.fm refuses as invalid (error 6) no longer holds back every later play: that batch is sent one by one and only the refused play is left out. A wrong key (other errors) still drops nothing. |
| Clock | Sign-in and Now Playing also wait for a trustworthy clock (certificate checks), as the plays already did; signing in before that says "The clock isn't set yet". |
| Sign-in | One sign-in request at a time. |

Host evidence: 512 Reborn tests (new: error-6 batch split with a stand-in curl, Now Playing decision, play numbers for
repeats, log-subsystem guard verified by mutation), strict Clippy, fmt.

## Device checks owed (owner installs over the air)

1. Now Playing on last.fm while a song plays, again after a pause of more than 30 s.
2. Offline: Wi-Fi off, a song past half, sleep with the **USB cable unplugged** (the charger refuses sleep), wake, Wi-Fi
   on: the waiting play is sent.
3. `/data/reborn/logs` now shows `lastfm`/`scrobble` events (sign-in, counted, sent).

## Known gaps

As Beta 7.7. Internet radio and FM are not scrobbled. Like every client app, the image carries the app key; misuse would
be answered by resetting the secret.
