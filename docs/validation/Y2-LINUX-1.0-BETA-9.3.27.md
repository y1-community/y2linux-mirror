# Y2Linux 1.0 Beta 9.3.27: Wi-Fi Share

Release `1.0.0-beta.9.3.27`, Y2Reborn `1.0.0-beta.9.3.27`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.27`, update
sequence 16400, root `2026.08-platform-v1.277`. Published as GitHub release `baseline-254` (build 254). Previous
release: [Beta 9.3.26](Y2-LINUX-1.0-BETA-9.3.26.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

**Wi-Fi Share** copies music onto the player without a cable. The player becomes a small website on your home
network; a phone, tablet or computer on the same Wi-Fi opens a file browser for Internal Storage and the SD card in
its browser. It is the answer to copying from an iPhone, which cannot open the player over USB
([#115](https://github.com/SchulzCode/Y2Linux/issues/115)). It was tried from a computer's browser against one
player; **phones and the QR code are not confirmed yet**.

## What changed

- **Wi-Fi Share.** **Settings → Library → Wi-Fi Share** shows a QR code, the player's address and a six-digit code.
  Scan the code or type the address into a browser on the same Wi-Fi and enter the six digits. The page lists
  Internal Storage and the SD Card and can upload files and folders, download, rename, delete and make folders.
  Back or **Stop Sharing** ends it; if anything changed, the player looks for new music by itself. Everything else:
  **[Wi-Fi Share](../WIFI-SHARE.md)**.
- **Careful by design.** The website exists only while its page is open on the player. Every start has a new code,
  and five wrong codes lock it for half a minute. Only the music storages are shared, exactly what a computer sees
  over USB. An upload arrives under a temporary name and gets its real name when complete, and the SD card is opened
  for writing only while something is written.
- **Not encrypted.** The connection is plain HTTP: a player on a home network has no certificate a browser would
  accept without warnings. Use it on a network you trust, not on public Wi-Fi.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests run the
  website on the build computer: the code and its lock, paths that try to leave the storage, listing, upload,
  download, rename and delete.
- **On one player** (test build 253 and this build): the page showed the QR code, address and code. From a
  computer's browser a wrong code was refused, both storages were listed, a folder was made, an MP3 and a FLAC with
  an umlaut in its name were uploaded, renamed, downloaded byte for byte and deleted, and an address reaching
  outside the storage was refused. Leaving the page stopped the website and started the scan.
- **Not tested**: any phone (also the iPhone this was made for) and the QR code, dropping whole folders, very large
  uploads and the speed, a router that keeps its devices apart ("client isolation": the page will then not open).

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.26](Y2-LINUX-1.0-BETA-9.3.26.md#known-issues) still applies. New:

- Wi-Fi Share is unconfirmed on phones; the connection is not encrypted.
- A broken upload starts again from the beginning; files cannot be moved between folders or played in the browser.
- The player's Wi-Fi is 2.4 GHz, so a USB cable is faster for a whole library.

## Technical appendix

- Reborn `crates/reborn-platform/src/share/` (a std-only HTTP/1.1 server on port 80: `mod.rs` sessions and routes,
  `http.rs`, `files.rs` path resolution inside the storage roots, `page.html` the browser's page), `app/reborn/src/
  share.rs` (runs it while the Library page with the share filter is on screen, keeps the player awake, scans after).
  SD writes go through the platform's write window. Plan:
  [REBORN-WIFI-SHARE.md](https://github.com/SchulzCode/Y2Reborn/blob/main/docs/ui/REBORN-WIFI-SHARE.md).
- Scope note for the new network service: [roadmap gap audit](../planning/roadmap-gap-audit.md).
- Images built from Linux `6e04e7b7`, Reborn `e4a76ca`.
