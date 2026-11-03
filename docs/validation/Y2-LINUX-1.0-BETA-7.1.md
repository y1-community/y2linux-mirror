# Y2Linux 1.0 Beta 7.1 (Baseline 62)

Status: **PUBLISHED as GitHub release `baseline-62`** (2026-10-16). Release `1.0.0-beta.7.1`, Reborn `1.0.0-beta.7.1`,
kernel `6.18.56-y2linux-1.0.0-beta.7.1`, update sequence 5850, Linux `e9427a9`, Reborn `81a907e`, git tag `baseline-62` in
both repositories. Built with `tools/production/release.py` in 118 s: container suite **743 OK**, all seal checks
PASS, no MediaTek firmware in any image. Milestone audit: BETA-7.1 in [roadmap-gap-audit](../planning/roadmap-gap-audit.md).

| Image | SHA-256 |
| --- | --- |
| BOOTIMG | `cf904fc8f85629585a95a474d73a0efaefc8f3ff475d60b5f63911a7cc1dcc96` |
| Y2ROOT | `06bbcd126d3741e176152c45ad3633652dc3b35acb06fcca07cfb3158a8f3da8` |
| RECOVERY | `3dc763ee2bb2cd351e46f4df62cbffedd41d5988c08ecaaf87e34062935ecbe4` |
| Y2DATA (no SSH key) | `a14b27724db22450056e687ff2985acffffeae89a636bdc617ee5a4ec608bfb2` |

## Since Beta 7

Owner (2026-10-16, on Beta 7): set the time zone automatically instead of scrolling a huge list; the USB screen takes a long
time or does not come at all, and it should not be possible to leave it; Check for Updates jumps to its result seconds after
it finished; the update should be one process without a second confirmation and without navigating away. Decisions:
automatic zone plus a country-first picker; Cancel only until the install starts; lock while a computer has the drives, with
a developer switch; ship as Beta 7.1.

| Part | Cause / change |
| --- | --- |
| USB screen late or missing | Reborn refreshed the platform status (≈1.6 s per call) only on Settings-type screens (every 15 s; 4 s on the computer page), never on Home, the library or Now Playing; on device the platform already reported MTP `Ready`. Now `client::usb_now()` reads `/sys/class/udc/*/state` and `/run/y2/usb-transfer.json` (RAM) every 0.5 s (1 s with the screen off) on every screen, lights the screen when a computer opens the drives, and overrides the older value of each status snapshot. |
| USB screen could be left | While a computer has the drives (MTP Ready/Copying) Back/Home/Select/Menu stay on the computer screen; playback, volume and Power keep working; "Unplug the cable to use the player". Service page: **Use While Connected** (developers, off by default; needed for SSH screen capture). Storage keeps "From a Computer" while a cable is in. |
| Check for Updates late | The check's reply was ignored until the next status refresh (≈2 s). Its own answer (`UpToDate`, or the manifest's release) now sets Up to Date / Available at once. |
| One-step update | **Update Now** (Available) and **Install Now** (an earlier download) run download → check → restart to install on one screen that cannot be left, after the charge rule (charger or ≥50 %) is checked up front. Cancel stops the download: Reborn sends SIGTERM to `update stage`, which removes the partial package and restores the check's summary until its commit point (queue), after which SIGTERM is ignored. 3-step progress. |
| Time zone | **Automatic** (default where no zone was chosen; an earlier choice stays): once per Wi-Fi network, `reborn_platform::geo` asks `https://ipapi.co/timezone/` (fallback `https://ipwho.is/?fields=timezone`) through the radio's bounded curl and accepts only zones of the built-in table. The picker lists Automatic, UTC and the countries A–Z (`tools/clock/countries.py` from iso3166.tab/zone.tab, limited to zones.rs); a country whose zones keep the same time is set directly, others show their cities west to east; a hand choice turns Automatic off. |

Host evidence: Reborn 483 tests, strict Clippy, fmt (new: usb_now from files, abortable download, geo validation, countries
table, one-step lock and Cancel, USB lock and developer switch, country picker); Y2Linux host suite 770 OK and
`test_platform_update` 25 OK (new: Cancel during a download). Previews of the new screens in Reborn's look and Classic.

## Device checks owed (after the owner's over-the-air install)

1. Plug the cable in on Home and in Now Playing: the computer screen within about a second; Back/Home do not leave it; music
   and volume work; unplugging returns. With the screen off, a computer opening the drives lights it.
2. Check for Updates on an up-to-date player: "Up to Date" the moment the spinner ends. When the next release exists: Update
   Now runs to the restart on one screen; Cancel during the download returns to "Update Available".
3. Time Zone: Automatic finds Europe/Berlin on Wi-Fi (Reborn log `clock/zone_found`), Germany sets Berlin directly, United
   States shows its cities.
4. Still open from Beta 7: UI v4 tour, `thermal_zone1` after a night, Power-sleep right after a wake.

## Known gaps

FM radio (Beta 8); no 12/24-hour switch; in-place decoder seek (≈1 s on Bluetooth); sleep ≈7.8–9.4 mA vs stock ≈1.3 mA;
Bluetooth range hardware-limited; no headphone-jack detection.
