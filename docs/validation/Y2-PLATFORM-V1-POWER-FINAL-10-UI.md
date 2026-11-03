# POWER-FINAL-10 — 09 plus Reborn Product UI v3 (validation receipt)

Status: offline-qualified; physical NOT_RUN. Identity: Linux build commit `04aa8f92080fba6cdefa4f73b5c7ca81d5c5bdaa` (later test-only commit `a08ad7967641badeead94ab399d73804afedbcf9`), Reborn 0.3.0 `f31c94c5775aee855589392447ba56ff1bf892ab`.
BOOTIMG `9d1f00a528f14824bef9eae5e65c4bd9cca00f38fc440e038c52e2cd67ecf0ca`, Y2ROOT `77a41a333ca27e9092dbafa37dc3f8da87ec6810c81398fa2258a1c116eafe16`. Fallback = sealed 09.

## Change
Reborn Product UI v3 (docs: Y2Reborn `docs/ui/REBORN-PRODUCT-UI-V3.md`; scope audit: Y2Linux `docs/planning/roadmap-gap-audit.md`, POWER-FINAL-10 entry).
Renderer: unchanged shader, new coverage-mask glyph/shape pages (luminance-alpha) and a 96×96 thumbnail atlas. Early splash regenerated (hand-off pixel-identical).
Navigation: Power hold ring instead of menus; flatter Settings; service menu behind seven taps on Version; EQ presets; covers via a background worker with a disk cache.
Linux: identity bump, regenerated `reborn-splash-mark.h`, splash bar dot, Open Sans licence installed under `/usr/share/reborn`; the Reborn version assertion in `tests/test_reborn_receipts.py` is now 0.3.0.

## Tests
Locked-container production suite PASS, 630 cases (first run failed one case, the stale 0.2.0 version assertion; fixed and rerun clean, both logs kept); 29 packaging-host and 27 splash PASS;
package, ELF closure, installed-ARM, inventory and privacy PASS. Reborn: fmt, strict Clippy, 355 workspace tests PASS; all 62 preview frames render identically through the production GL code on a host GLES2 (Mesa) and the Python rasteriser.
## Not verified
On-device rendering (luminance-alpha and non-power-of-two textures on the Mali-400), frame times, memory (UI textures grow about 4.4 → 9 MB), cover decode time, the Power hold feel, shutdown timing
(user grace 10 s, low-battery grace 3 s; the platform deadline still fires without an acknowledgement). Not implemented from the design: automatic "Connected to a computer" screen, the charging screen with the display off,
the download progress ring, the Bluetooth pairing progress card.
## Verdict
**IMPLEMENTATION_READY_PHYSICAL_NOT_RUN**
