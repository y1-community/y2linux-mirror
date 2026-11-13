# Y2Linux 1.0 Beta 9.3.26: FM radio, Next between films, pairing from a car

Release `1.0.0-beta.9.3.26`, Y2Reborn `1.0.0-beta.9.3.26`, kernel `6.18.56-y2linux-1.0.0-beta.9.3.26`, update
sequence 16300, root `2026.08-platform-v1.274`. Published as GitHub release `baseline-251` (build 251). Previous
release: [Beta 9.3.25](Y2-LINUX-1.0-BETA-9.3.25.md).

This report is written for people who use the player. A short technical appendix is at the end.

## In one paragraph

**FM radio is back** for Y2s whose hardware receives FM: Radio opens as FM Radio and Internet Radio. It played on
one player, a first-revision Y2 that receives only noise, so **reception of a real station is still unconfirmed**.
Also new: **a car can pair with the Y2 from its own screen** ([#116](https://github.com/SchulzCode/Y2Linux/issues/116)),
and **Next and Previous during a film go to the next or previous film** instead of a song
([Y2Reborn #128](https://github.com/SchulzCode/Y2Reborn/issues/128)).

## What changed

- **FM radio.** Radio on Home opens as **FM Radio** and **Internet Radio** on a player with the tuner's files. Wired
  headphones are the antenna. **Search for Stations** lists what it finds; a station plays like an internet
  station, with frequency and Stereo/Mono on Now Playing. **Hide FM Radio** (on an empty search) and
  **Settings → Customize → Browsing → FM Radio** switch it off. A player set up before this release needs
  `system.img` copied into the **Firmware** folder once more for the tuner's two files. Everything else:
  **[FM radio on the Y2](../FM-RADIO.md)**.
- **Pairing from a car.** Cars pair from their own screen: they search, list the devices that make themselves
  visible, and show a number to confirm on both. The Y2 never made itself visible, so it was never listed (the
  reporter's error report showed exactly that). Now **Settings → Bluetooth → Search for Devices** also makes the
  player visible as **Y2Linux** for 2 minutes. The car's number appears on the Y2 to confirm; after that the car's
  audio connection needs no second question, and the car is remembered and reconnects like headphones paired on
  the Y2. Bluetooth off, sleep, a pairing started on the Y2 or the 2 minutes end the visibility.
- **Films in order.** A film chosen under Videos plays with the Videos list as its queue: Next and Previous go to
  the film below or above, in the list's order also with Shuffle on. Before, a film was put in front of the music
  queue, so Next went on with a song.

## How it was tested

- **Tests**: the platform's full check, Reborn's tests and the tests of the release images passed. New tests: a
  simulated Bluetooth stack in which a car pairs from its side (the player becomes visible on a search, the car's
  number is asked, the yes is enough for its audio, it is trusted, the player hidden again and the car connected),
  and the Videos list handing its order to Next and Previous.
- **On one player** (first revision, test builds 249 and 250): the FM tuner switched on and reported its chip,
  tuned across the band (signal −102 to −107 dBm everywhere: noise), the search ran, a station played in Reborn,
  pausing switched the tuner off after three seconds and resuming on again, Wi-Fi and Bluetooth kept working.
- **Not tested**: reception of a real station (needs a later-revision Y2), sleep with FM playing, FM over Bluetooth
  by ear; pairing with a real car (the reporter's Tesla); this exact build on a player.

## Updating

Settings → System → Software Update → **Update Now**. Your music, playlists, settings, Wi-Fi networks and Bluetooth
pairings stay. **Restore Previous System** (Settings → System → About, tap Version seven times, then Diagnostics) goes
back to the version you had.

## Known issues

Everything listed for [Beta 9.3.25](Y2-LINUX-1.0-BETA-9.3.25.md#known-issues) still applies. New:

- FM reception is unconfirmed; a first-revision Y2 receives only noise, and its search may list one or two
  frequencies that are not stations.
- FM stops when the player goes to sleep by the Power button and does not come back by itself; no RDS station names.
- Whether every car lists a music player (the Y2 announces itself as portable audio, not as a phone) is unknown.

## Technical appendix

- FM: kernel `connectivity/fm.c` (MT6627 on STP channel 1, the stock firmware's commands), AFE capture `Y2 FM
  Capture` at 48 kHz; platform `commands/service/radiofw/fm.rs` takes the tuner's two files from `system.img`;
  the sleep helper switches a left-on tuner off; Reborn `crates/reborn-platform/src/fm.rs`, `app/reborn/src/fm.rs`.
  Plan and results: [BETA-8-FM-PLAN.md](../planning/BETA-8-FM-PLAN.md).
- Car pairing: `y2-radiod` `bt/agent.rs` (`show_to_others`: Discoverable and Pairable for 120 s with BlueZ's own
  timeouts; `welcomed`/`adopt_welcomed`), started from the scan in `bt/reconcile.rs`; described in
  [radio-daemon.md](../architecture/radio-daemon.md). Fake-BlueZ scenario `a_car_pairs_from_its_side`.
- Films: Reborn `views_videos::films` is the Videos order, `reborn_core::queue::keeps_order` keeps a film
  collection unshuffled.
- Images built from Linux `302077e2`, Reborn `acafbda`.
