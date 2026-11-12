# Video playback plan

Status: **owner test build 243 plays films (2026-11-12).** What works, what was measured and what is open:
[Videos on the Y2](../VIDEO.md). Nothing is published; every commit is local.

| Step | State |
|---|---|
| 0 Audit and roadmap | Done (audit entry VIDEO) |
| 1 Decoder reverse engineering | Done for H.264 and MPEG-4 Part 2 ([register read-out](../knowledge/video-decoder.md)) |
| 2 Bring-up | Done: gate B met (pictures byte-identical to FFmpeg); no M4U needed |
| 3 Kernel driver | H.264 (frames, fields, MBAFF) and MPEG-4 Part 2 (progressive) done (`kernel/platform/vdec*.c`); the sleep/wake part of gate C open |
| 4 Userspace decode | Done: FFmpeg with our hardware path and the software decoders; hardware against software compared on nine files |
| 5 Playback engine | Done as a second reader following the sound's position (Reborn `app/reborn/src/video.rs`) |
| 6 Renderer | Done: one picture texture; shrinking and colour on the CPU |
| 7 Library and UI | Videos place and list, thumbnails, film view on Now Playing, resume; no track or subtitle choice |
| 8 Subtitles | Text tracks and files beside the film; no switch |
| 9 Skins | Open: the film view is Reborn's own in every skin |
| 10 Power and sleep | Screen stays on, decoder off when idle; drain and sleep/wake not measured |
| 11 Qualification and install | Test builds 236 to 243 installed on the owner's player; the device tour by the owner is open |

## Decisions taken while building (2026-11-12, to be confirmed by the owner)

| Plan said | Now | Why |
|---|---|---|
| V4L2 stateless decoder (request API) | Own character device `/dev/y2-vdec` (`kernel/platform/y2-vdec.h`): size the buffers, map them, one call per picture | FFmpeg 9.0.2 has no request-API path, and MPEG-4 Part 2 has no stateless interface in Linux at all; both would have been ours to write anyway. Userspace sees no register and no physical address. |
| FFmpeg's hardware path | Our own FFmpeg hardware path (a patch in the build), FFmpeg keeps parsing and reference handling | Same reason; FFmpeg's H.264 bookkeeping is the tested part we do not want to rewrite. |
| H.264 Baseline/Main/High on the hardware | Progressive 4:2:0 8-bit streams on the hardware; interlaced and field-coded streams, other chroma formats and anything above 1920×1088 in software | Field decoding triples the reference-list work for material a pocket player rarely sees; it can follow. |
| Frames to the renderer as dmabuf or texture upload with a conversion shader | The decoder's block layout is unpacked and scaled to the 480×360 screen on the CPU (one pass), then uploaded as small Y and CbCr textures; the shader only converts colour | The Mali-400's fragment shader works in 16-bit floats, too coarse to address blocks in a 1080p picture; a small upload is cheap and is the same path for software-decoded frames and thumbnails. |
| Memory | No reserved pool and no CMA: every buffer is at most 4 MiB of contiguous pages (a 1080p frame is 3.1 MiB) | 147 free 4-MiB blocks were counted on the running player; no memory-map change. If long uptimes fragment memory, a pool is added and named in the audit first. |

## Owner decisions (2026-11-11)

| Question | Decision |
|---|---|
| Hardware decoder | A self-written driver is part of the first delivery. No MediaTek libraries or firmware on the player. |
| Formats | H.264, MPEG-4 Part 2 (with H.263/Xvid/DivX), VP8, VP9. No HEVC, no AV1. |
| Delivery | One test build, installed on the owner's player only. Publishing is a later, separate decision. |
| In the first version | A Videos section, thumbnails, resume position, subtitles, skin support. |
| Not required in the first version | Lip sync over Bluetooth (sound over Bluetooth still plays; the picture is not delay-matched). |
| Place in the product | Core function of Y2Reborn, not a plugin. |

## What the stock firmware shows (y2_v3.2.0_FM-20260813, read with REA)

Observed in the shipped files:

- `media_codecs.xml` lists MediaTek decoders for MPEG-4, H.263, AVC (H.264), VP8, VC-1, DivX, Xvid and
  Sorenson H.263. VP9 is Google's software decoder.
- `libvcodecdrv.so` (231 KB, **symbols not stripped**) holds the hardware decoding logic in userspace:
  bitstream parsing (`parseSPS`, `parsePPS`, `parseSliceHeaderHW`), reference lists
  (`H264_HAL_SetPRefPicListReg`, `H264_HAL_SetBRefPicListReg`), register programming by block
  (`H264_HAL_WriteVLD`, `…VLDTOP`, `…MC`, `…AVCVLD`, `…AVCMV`, `…PP`, `…MISC`, `…SYS`), start
  (`H264_HAL_DecStart`) and wait (`H264_HAL_WaitISR`). MPEG-4 has the same shape
  (`mpeg4_part2_risc_settings`, `mpeg4_hw_reg_init`, `divx311_risc_settings`).
- VP8 and VC-1 are separate **software** libraries (`libvp8dec_sa.ca7.so`, `libvc1dec_sa.ca7.so`). So on
  this chip the hardware decodes H.264 and the MPEG-4 family only; VP8/VP9 are software even on stock.
- No decoder firmware file exists in `/system/etc/firmware` (only modem, Wi-Fi, Bluetooth patch, FM).
- The kernel side is thin: `libvcodec_utility.so` opens `/dev/Vcodec` and uses ioctls for hardware lock,
  wait-for-interrupt, power users, non-cached buffers and register `mmap`; buffer addresses go through
  MediaTek's M4U (IOMMU) helpers (`VcodecM4u_*`, `eVideoAllocMVA`).
- A size check against 1920×1080 (`0x1fa400` pixels) appears in `H264_CheckHWParameter`.

Inferred, to be confirmed in step 1: the register blocks are the same decoder family that mainline
Linux drives in `mtk-vcodec` for later chips, which would give register names and a driver skeleton.

Not yet known: the decoder's register base and interrupt on this board, its power domain and clock
gates, whether it works with plain physical addresses or needs the M4U, and the exact stock H.264
profile/level limits. Our kernel today has the `VDEC_SEL` clock mux id and nothing else for the decoder:
no power domain, no SMI larb for it, no M4U driver, no V4L2 device.

## What the player gives us

- Reborn already demuxes MP4/MOV through FFmpeg 9.0.2; video decoders are a build option.
- The renderer draws through GLES2 on a Mali-400 at 60 fps on 480×360; `/dev/dri` has two cards.
- Four Cortex-A7 cores at up to 1.3 GHz, 948 MB RAM, root partition 25 % used.
- The audio sink reports its delay, which is the clock picture timing needs.

## Design

```
file ─ FFmpeg demux ─┬─ audio packets ─ existing audio path ─ sink (master clock)
                     └─ video packets ─ decoder ─ frame queue ─ renderer (YUV→RGB shader) ─ panel
                                          │
                        H.264 / MPEG-4: hardware decoder (new kernel driver)
                        VP8 / VP9, and any file the hardware refuses: FFmpeg software
subtitles: SRT / embedded text ─ timed lines ─ drawn by the UI over the picture
```

- **Kernel driver:** a V4L2 stateless decoder (request API) for H.264 and MPEG-4 Part 2, written from the
  register behaviour read out of `libvcodecdrv.so`. Parsing stays in userspace (FFmpeg already does it);
  the driver programs the registers per frame, as the stock library does.
- **Software path:** FFmpeg's `vp8`, `vp9`, plus `h264` and `mpeg4` as the fallback when the hardware
  refuses a stream (unsupported profile, size, error).
- **Frames to the screen:** decoded NV12 buffers go to the renderer as dmabuf-backed EGL images when
  Lima supports it on this setup, else as uploaded textures; a shader converts to RGB and scales to
  480×360 keeping aspect ratio.
- **Clock:** audio is the master; frames are shown, held or dropped against the sink's reported
  position. Without an audio track the wall clock is the master.
- **Library:** video files (`.mp4 .m4v .mkv .webm .avi .mov`) go into their own table and a **Videos**
  entry on Home; they never appear in Songs. A file with video and audio is a video.

## Steps (one delivery, gated inside)

Nothing is installed until all steps pass. Each step ends with evidence; a failed gate stops the
build and comes back to the owner with what was found.

0. **Audit and roadmap.** Roadmap/gap audit for the new hardware subsystem; add the video milestone to
   the roadmap. Documentation only.
1. **Decoder reverse engineering (REA, read-only).** From `libvcodecdrv.so`, `libvcodec_utility.so` and
   the stock kernel: register map per block, the per-frame H.264 and MPEG-4 sequences, buffer layout
   (frame buffers, motion-vector and prediction buffers), interrupt and timeout handling, power-up
   order, clock gates, M4U use. Result: a register and sequence document under `docs/knowledge/`.
   **Gate A:** the sequence is complete enough to write a driver, and no closed code is needed at run time.
2. **Bring-up on the owner's player** (needs explicit owner authorization per live step, as with earlier
   register work): power domain and clocks on, registers readable, one interrupt, then one H.264
   I-frame decoded to memory and compared bit-exactly with FFmpeg's output for the same frame.
   **Gate B:** bit-exact I-frame. If the hardware only works through the M4U, an IOMMU driver joins
   this step.
3. **Kernel driver.** V4L2 stateless H.264 (Baseline/Main/High, up to the stock limit) and MPEG-4 Part 2;
   suspend/resume, deep-idle and screen-off rules, error recovery (reset on a bad stream), status and
   report fields. **Gate C:** FFmpeg's conformance streams for the supported profiles decode
   bit-exactly on the player; sleep and wake with an open decoder leave the display and audio intact.
4. **Userspace decode.** FFmpeg build with the four decoders and the V4L2 request hardware path
   (whether FFmpeg 9 carries it upstream or needs the known patch set is checked first), fallback rule
   hardware → software, measured CPU load and frame drops per resolution.
5. **Reborn playback engine.** Video stream beside the audio path: decode thread, frame queue, clock,
   frame drop, seek to keyframes, pause/resume, end of file, errors as messages. Tests with recorded
   streams on the host.
6. **Renderer.** YUV shader, scaling and letterboxing, zero-copy import if available, no tearing; frame
   timing measured with the existing performance trace.
7. **Library and UI.** Videos table (schema step with rollback), scan rules, thumbnails (one decoded
   frame per file, cached like covers), Videos list, player screen (play/pause, wheel seek, volume,
   time, subtitle and audio-track choice), resume position per file, Quick Menu and sleep-timer
   behaviour, `rebornctl` actions for tests.
8. **Subtitles.** SRT beside the file and embedded text tracks (SubRip, mov_text, ASS as plain text),
   drawn with the existing fonts (CJK included). Bitmap subtitles are out.
9. **Skins.** Layouts `videos` and `video_player` in the skin format, defaults for every shipped skin,
   preview cases, documentation for skin authors.
10. **Power and sleep.** Screen stays on while a video plays; pause follows the existing screen-off and
    sleep rules; CPU level and deep-idle rules for video; battery drain measured for hardware and
    software decoding.
11. **Qualification and install.** Platform gate, Reborn checks, the architecture-change qualification
    (new kernel driver, schema, packaging), then one test build installed on the owner's player with a
    device tour: each format, seek, subtitles, resume, wired/speaker/Bluetooth sound, sleep/wake, an
    hour of playback, an error report containing the new fields.

## Risks

| Risk | Effect | What the plan does |
|---|---|---|
| The hardware cannot be driven from the userspace library's evidence alone | No hardware decoding; by the owner's decision nothing ships | Gates A and B come before any product code; the owner decides again there |
| The decoder needs the M4U (IOMMU) | An extra kernel driver, which also touches display memory paths | Found in step 1, built in step 2, qualified with the display |
| Live register work can wedge the display/SMI path (seen before: 40/41, 8.3 OTA) | Dark screen until restart | Read-only first, every live write authorized by the owner, recovery path checked before each session |
| H.264 hardware limits (profile, level, interlaced, 10-bit) | Some files fall to software or are refused | Fallback rule and a clear message; limits documented for users |
| Software VP9 at high resolution | Frame drops above roughly 480p–576p | Measured in step 4; larger files get a message instead of a slideshow |
| Lima cannot import the decoder's buffers | One copy per frame | Texture upload fallback; cost measured in step 6 |
| Patents (H.264, MPEG-4) | A publishing question, not an engineering one | Owner-only test build; decided again before any release |
| Size of the work | Weeks; the driver steps cannot be scheduled reliably | Gates report real findings instead of dates |

## Open questions for later (not blocking the plan)

- Bluetooth lip sync: fixed delay per codec, or measured per headset.
- Video output of internet radio streams or podcasts: not planned.
- Whether video ever leaves the owner's player (patents, release 49's firmware-free rule is not affected:
  the driver uses no vendor blob).
