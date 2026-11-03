# Installing the Y2 recovery image (one time)

<!-- knowledge-base-scope: owner-runbook -->
Over-the-air updates ([contract](platform-ota-v2.md)) are installed by the Y2 recovery image in the
RECOVERY partition. It replaces the stock Android recovery once. Later releases bring their own
recovery image inside the signed update, and the player installs it by itself.

Do this in the same SP Flash Tool session as the normal update of this release (BOOTIMG + ANDROID with
`MT6582_preserve_data_scatter.txt`), after that download finished. The order does not matter.

## Before the first write

1. Check the files: `sha256sum -c SHA256SUMS` in the package folder. `recovery-install/recovery-install.json`
   lists the image hash.
2. Keep a copy of the current RECOVERY partition. On a device running Y2Linux it is already saved in the
   evidence archive `20261015/128-ota-entry-baseline/emmc-readback/RECOVERY.bin`
   (`b3389226…be26`, stock Android recovery). With SP Flash Tool: Readback, EMMC_USER,
   start `0x2d80000`, length `0x1000000`, in the coordinate mode noted in `metadata/readback-plan.json`.

## Flash

1. SP Flash Tool v5.2032, **Download Only**, load `recovery-install/MT6582_recovery_scatter.txt`.
2. Exactly **one** row is selected: **RECOVERY → RECOVERY.img**. Every other row must be unselected and
   show no file. Stop if any other row is selected or SPFT asks to format or reports a table change.
3. Download. Disconnect, then start the player normally (Power).

## Check

On the player: Settings → About & Updates shows the update page. To prove LK starts the recovery image,
run once over SSH:

```
y2-platform update recovery-status     # misc_request "none", recovery_image_valid true
y2-platform update probe-recovery      # the player restarts into the recovery image
```

The player restarts into the recovery image, shows **Recovery works** and restarts into the normal system
after a few seconds (with the owner's `{"mode": "poweroff"}` override in `/data/updates/restart.json` it
turns off instead; press Power to enter the recovery, and again to start the system). `/data/updates/recovery-probe.json` records the recovery boot
(`boot_mode=2` from LK, `recovery_image=1` from the kernel).

## Undo

Flash the saved RECOVERY readback (or the factory `recovery.img`) to the RECOVERY row with the same
one-row scatter. The normal system does not need RECOVERY; only over-the-air updates do.
