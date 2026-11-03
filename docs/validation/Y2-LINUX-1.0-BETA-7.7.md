# Y2Linux 1.0 Beta 7.7 (Baseline 70)

Status: **PUBLISHED as GitHub release `baseline-70`** (2026-10-16). Release `1.0.0-beta.7.7`, Reborn `1.0.0-beta.7.7`,
kernel `6.18.56-y2linux-1.0.0-beta.7.7`, update sequence 6150, Linux `bcb7815`, Reborn `f117586`, git tag `baseline-70` in
both repositories. Built with `tools/production/release.py` in 135 s: container suite **745 OK**, all seal checks PASS,
no MediaTek firmware in any image. Labels 68 and 69 belong to FM radio test builds on branch `beta8-fm` (never
published). Milestone audits: LASTFM and BETA-7.7 in [roadmap-gap-audit](../planning/roadmap-gap-audit.md); plan and
security model: [LASTFM-SCROBBLE-PLAN](../planning/LASTFM-SCROBBLE-PLAN.md).

| Image | SHA-256 |
| --- | --- |
| BOOTIMG | `7a01184e35e3d1340970bf11704fedce0e8c7e6e0481f3b98efb6b73e51548ef` |
| Y2ROOT | `b35a6f5dd69c6f8329f0662a69b9aa8dcdaab82872cd7c756ac67662bf82237a` |
| RECOVERY | `6d5cd66e8a770f891374e7e37c92445c12f36193940933cbc82bad3fd0c99d82` |
| Y2DATA (no SSH key) | `79a2de02276189d2d93d47c35cb7872d837a810fac1f03398dff46567949bb42` |

## Since Beta 7.6 (Reborn `36e6994`)

| Part | Change |
| --- | --- |
| Scrobbling rules | A library track over 30 s counts once heard for half its length or 4 minutes; pauses and seeks add nothing; a return to the start is a new play; radio and untagged files never count. Plays during an unknown clock are dated later in the same boot (CLOCK_BOOTTIME) or dropped. |
| Offline queue | Each counted play is fsynced to `/data/reborn/state/scrobbles.jsonl` (torn-line safe, 10 000 cap, one process-wide lock; a send removes only exactly what it sent). Sent in batches of 50 when Wi-Fi is up, with back-off plus a random spread; never wakes the player. |
| Last.fm | Sign-in by QR code (Last.fm's desktop flow: token, approval on the phone, session), password fallback (one request, never stored). Now Playing, Send Now, Disconnect. The form travels on curl's stdin (no secret in the process list); HTTPS with certificate checks only. A revoked session keeps its plays for the same account; another account never receives them. |
| `.scrobbler.log` | Optional Rockbox AUDIOSCROBBLER/1.1 log at the top of Internal Storage, without an account. |
| Settings → Last.fm | New page; the sign-in code is Reborn's own drawing in every skin (near-black/near-white survive palette remaps). Settings shows eight places. |
| Secrets | The app's key and shared secret come from the owner's file outside git (0600, exact format) into the image as `/usr/share/reborn/lastfm-api.json` (root, 0600). The listener's session stays on Y2DATA (0600), never in images or logs; Disconnect, Reset Settings and the owner settings reset forget it and its plays. Release privacy check refuses session, queue and log files in images. |

Host evidence: 509 Reborn tests (rules, queue race, MD5 RFC 1321 vectors, signatures, stand-in curl, UI incl. the password
never drawn or printed, QR decoded by zbar from the encoder and from Classic/Paper renders), strict Clippy, fmt; 772
Y2Linux host tests. The key pair was confirmed by Last.fm (auth.getToken answered) before the build; the image holds the
key file as root 0600.

## Device checks owed (owner installs over the air)

1. Settings → Last.fm → Connect with Your Phone: scan, allow, "Scrobbling as …".
2. A song past halfway appears on last.fm; Now Playing while it plays.
3. Wi-Fi off, a counted play, sleep, wake, Wi-Fi on: the waiting play is sent.
4. Still open: the 7.6 update screens' frame rate (seen on this update's download), 7.5/7.4 checks.

## Known gaps

As Beta 7.6. No live end-to-end scrobble has been made yet (first one on the owner's player). Internet radio and FM are
not scrobbled. Like every client app, the image carries the app key; misuse would be answered by resetting the secret.
