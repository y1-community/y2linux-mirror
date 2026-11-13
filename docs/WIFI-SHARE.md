# Wi-Fi Share: copying music without a cable

Status: **new in Beta 9.3.27 (13 November 2026).** Tried from a computer's browser against one player; phones,
the QR code and very large uploads are not confirmed yet. Tell us how it goes.

Wi-Fi Share turns the player into a small website on your home network. Any phone, tablet or computer on the same
Wi-Fi opens it in a browser and sees a file browser for **Internal Storage** and the **SD Card**: no cable, no
program to install, and it works on an iPhone, which cannot open the player over USB
([#115](https://github.com/SchulzCode/Y2Linux/issues/115)).

## Using it

1. On the player: **Settings → Library → Wi-Fi Share**. The page shows a QR code, an address such as
   `192.168.1.23`, and a six-digit code.
2. On the phone or computer, on the same Wi-Fi: scan the QR code with the camera, or type the address into the
   browser. Enter the six-digit code once.
3. Pick a storage and a folder. **Upload files** or **Upload a folder** (on a computer you can also drop files and
   folders onto the page). **Download**, **Rename**, **Delete** and **New folder** are beside each entry.
4. When you are done, press Back or **Stop Sharing** on the player. If anything changed, the player looks for new
   music by itself.

Sharing runs only while that page is open on the player, and the player stays awake for it. Leave the page and the
website is gone.

## Good to know

- **Who can get in:** anyone on the same network who knows the code on the player's screen. Five wrong codes lock
  the door for half a minute, and every start has a new code. The connection is not encrypted (a home network has
  no certificate for it), so use it on a network you trust, not on public Wi-Fi.
- **What is shared:** the music storages, exactly what a computer sees over USB. Nothing else of the player.
- **Uploads** arrive under a temporary name and get their real name when they are complete: a transfer that breaks
  leaves no half song. A file that is already there is kept unless you agree to replace it.
- **The SD card** is opened for writing only while something is written, as with a USB copy.
- **Speed** depends on your Wi-Fi; the player's radio is 2.4 GHz. A USB cable is faster for a whole library.
- **Large files:** a FAT32 card takes no file above 4 GB.

## Not there yet

Encrypted connections (HTTPS), resuming a broken upload, playing music in the browser, moving files between folders.

How it is built: [plan](https://github.com/SchulzCode/Y2Reborn/blob/main/docs/ui/REBORN-WIFI-SHARE.md).
