# POWER-FINAL-45 — over-the-air test package for 44's recovery screen

Status: **INSTALLED over the air, PASSED** (2026-10-16, archive `20261016/149`). Identity-only candidate on top of 44 (commit `977e14f`),
delivered over the air to test 44's DSI read-back in the restart-entered recovery. Package
`out/y2linux-platform-v1-power-final-candidate-45/` (fallback = 44), receipts `out/deep-sleep-final/build45-receipts/`.
BOOTIMG `be4b5b1d7f178187c093595b10e3bf02ae067a2d2a91d0cb456353a11f97e9ab`, Y2ROOT
`2f87e4b5fb259732c3384d5ed47202653a597d4333d0c33246c1989d586b1574`, RECOVERY
`c5dbde18cae7b54d0bd5e651741717ddacf9b9dd99d144a6f2dc56e6e7dbc546`. Kernel `6.18.56-y2linux-platform-v1-power-final-45`,
update sequence 4500. Container suite **718 OK**, all seal checks PASS. Signed with the release key:
`…-45-ota-release/`.

## 44 results

- **Flashed by the owner (2026-10-16);** cold start lit (archive `20261016/148`, boot `348bdf1e`): hand-over read-back
  `ps=000305a0 vact=00000168` (no lag), no power-cycle retry, vblank 16 → 217, taint 0.
- The 44 package staged earlier was still queued; cancelled (already installed), then 45 staged with the restart
  switch on: `Queued …candidate.45 4500`.
- Staging from a copy on Y2DATA failed with `update_data_space_reserve`: staging needs the package plus a quarter of
  the root image plus the 96 MiB reserve free (≈281 MiB), and the cancelled package (57 MiB) stays in `pending`
  until a new one replaces it. Staged from RAM instead. Follow-up: cancel should delete the pending package.

## Owner steps

1. **Turn Off and Install** in Reborn (the switch makes it restart). Watch the progress screen.
2. The agent checks the recovery log (`DSI read-back`/`power-cycling` lines, `recovery-display.txt`), `Acknowledged`
   and the new boot's display.

## Device results — PASSED

- **Over-the-air install 44 → 45 without a Power press** (release key, restart switch on): recovery screen **lit**
  (hand-over read-back `vact=00000168`, vblank 17 → 219), install 54 s (verify 1.8 → backup 3.6 → system 34.9 →
  check 48.1 → boot 52.9 → installed 53.7), restart into 45 lit (boot `a40337a9`, reason 4, vblank 220), taint 0,
  health `Acknowledged`, package removed. Owner: "everything is working very nicely", fast, restarts perfect.
- 44's retry did not run: the register lag did not occur on this boot, so the retry is still untested on a real lag.
- Y2DATA cleanup (owner-approved, after archives 148/149): old test logs and qualification directories removed;
  the fresh 44 backups (`previous*.gz`) kept for rollback. 366 → 79 MiB used, 635 MiB free.
