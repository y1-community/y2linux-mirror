# MT6582 AFE register/DMA laboratory (temporary, development only)

`y2_afe_lab.ko` is a generic, bounded instrument for finding out which AFE
memory-interface and interconnect features exist in the exact MT6582 silicon.
It is **not** product code and must never be part of a candidate.

* Register access is limited to the AFE window (`0x11220000`, offsets 0-0x7fc).
* `hold 1` owns one runtime-PM reference of the production AFE device so the
  audio clocks stay on; always finish with `hold 0`.
* DMA slots are ordinary coherent buffers below 4 GiB; no raw physical memory.
* The installed kernel has `CONFIG_MODULE_UNLOAD=n`: once loaded it stays until
  reboot, so experiments are driven through the command file, not reloads.
* Hold `/tmp/y2-platform-v1-device.lock` for the whole device session.

Build against the exact installed kernel (see `tools/development/audio-observer`):

    python3 tools/build/run.py --output out/highres-observer-build -- \
        make -C /src O=/build/kernel M=/build/afe-lab modules

Commands are written to `/sys/kernel/debug/y2_afe_lab/cmd`, results read from
`.../out`: `hold 0|1`, `r <off> [n]`, `dump <a> <b>`, `w <off> <val>`,
`m <off> <mask> <val>`, `dma_alloc|dma_free|dma_fill32|dma_zero|dma_peek|dma_info`.
Writes are only for source-backed hypothesis tests with the I2S output disabled,
the headphone switch off and no PCM stream running.

## i2s_capture (bounded serializer-pad experiment)

`arm 1` then `i2s_capture src=1|2|3 [rate=0|1|2|4] [n=<samples>] pat=<w0,..> [pat2=<..>] [w=<off>:<val>,...]`.
One-shot and self-restoring: refuses unless armed, the AFE is idle (no stream or
PM reference), the kernel identity matches and no other lab experiment is active;
it programs the selected memif ring(s) (pattern words are `R<<16|L`), the
whitelisted routing/gain registers in `w=`, an I2S output (I2S format, 32-bit
slot, documented rate code, default 8 kHz), samples GPIO43/44/46 from the
kernel, then always disables I2S/memifs, restores and verifies every touched
register and releases clocks. Raw samples: `debugfs/y2_afe_lab/sla.bin` (u16 per
sample of GPIO32-47 DIN). Decode with `i2s_decode.py` (BCLK is the reference).
Keep the headphone switch off and no ALSA stream open. It never leaves I2S enabled.
