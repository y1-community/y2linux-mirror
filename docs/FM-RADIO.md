# FM radio on the Y2

Status: **in Beta 9.3.26 (2026-11-12; owner test builds 249 and 250 before it).** FM has
played on one player, a first-revision Y2 that receives no stations; nobody has confirmed reception under Y2Linux
yet. This page says who can use it, how, and what is known.

## Which players

The Y2's radio chip contains an FM tuner in every unit, but not every unit can receive with it.

| Your Y2 | FM |
|---|---|
| A later revision (the stock software's FM radio found stations on it) | Supported: this is the player FM is for |
| The first revision (the stock FM radio never found a station) | The tuner switches on and tunes, but only noise arrives. No software can change that: hide FM (below) |

If you do not know which you have, try a search. It takes a few seconds and harms nothing.

## Setting it up

FM needs two small files of MediaTek's from the same `system.img` that Wi-Fi and Bluetooth need (the Innioasis Y2
firmware package v3.2.0). Y2Linux may not hand them out.

- **A new installation** takes them along when you copy `system.img` into the **Firmware** folder
  ([INSTALL](INSTALL.md)).
- **A player that was set up before FM arrived** has Wi-Fi and Bluetooth but not these two files, and the player
  deleted its copy of `system.img` back then. Make a folder named **Firmware** on Internal Storage (or the SD
  card) and copy `system.img` into it once more. Within a minute the player takes the two files and deletes the
  copy; Wi-Fi and Bluetooth are not interrupted. Until then FM Radio says "Needs the tuner firmware".

## Using it

- **Radio** on Home now opens as **FM Radio** and **Internet Radio**. (On a player without FM, or with FM hidden,
  Radio is the internet stations as before.)
- Plug in **wired headphones**: their cable is the antenna, also when the sound goes to Bluetooth.
- **Search for Stations** goes through 87.5 to 108 MHz and lists what it finds by frequency. **Search Again** is
  the last row of that list; search again in another place.
- A station plays like any radio station: Now Playing shows the frequency and Stereo or Mono, the volume, the
  outputs and Favorites work as usual, and it keeps playing with the screen off.
- FM stops when the player goes to sleep by the Power button, and does not come back by itself after the wake.

## Hiding FM

On a player that receives nothing, FM Radio is only in the way.

- The page of a search that found no station has **Hide FM Radio**.
- **Settings → Customize → Browsing → FM Radio** is the switch for it, and brings FM back.

## What was checked, and what was not

On a first-revision Y2 (test builds 249 and 250): the tuner switches on and reports the right chip, tunes across
the band, and reads a signal level (−102 to −107 dBm everywhere: noise); its sound arrives as a steady stream; the
search runs and saves its result; a station plays in Reborn; pausing switches the tuner off after three seconds
and resuming switches it on again; Wi-Fi and Bluetooth keep working beside it. The commands the driver sends are
the stock firmware's, compared byte for byte in an emulator.

Not checked: **reception of a real station** (needs a later-revision Y2), sleep with FM playing, FM over Bluetooth
by ear, long listening and battery use. There is no RDS (station names) yet. The search on the first-revision
player listed one or two frequencies that are probably not stations.

How it is built: [FM plan and results](planning/BETA-8-FM-PLAN.md).
