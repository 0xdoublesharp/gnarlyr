# Optimization ideas from other projects

Techniques from Rockbox, community firmwares for the Echo family and codec projects that could make Gnarlyr faster or save battery. Gnarlyr references are `firmware/` paths at `integration/next` 6ff88b36. The battery branch `perf/battery-2` (a638ed22) is noted where it already covers an item. Stock findings are in `docs/stock-hardware-notes.md` on `research/stock-hardware`.

Gains are estimates unless they are attributed. A figure credited to another project is that project's measurement, made on its own hardware. Nothing here was measured on the full-size Echo. Every item needs a player check before it can be called working.

## Ground rules that shape the list

- **No voltage scaling.** Stock's profiles change only frequencies. Racing to idle therefore saves no dynamic energy per cycle. Savings come from doing fewer cycles, and from lowering the PLL, bus and SRAM clocks that keep running through WFI.
- **All code already runs from on-chip SRAM.** Rockbox's IRAM/ICODE placement doesn't apply. Core 1 runs without memory wait states at 100 MHz and below (`platform/device/logic/core1_control.c:90`).
- **Thumb-2 only.** Cortex-M3 has SMULL/SMLAL (3–7 cycles), MLA (2), CLZ, SSAT/USAT and LDM/STM. It has no DSP extension: no SMULWx, QADD or SIMD. Assembly written for ARM state with RSC, register-shifted register operands or conditional execution without IT blocks needs rewriting.
- **Licences.** Gnarlyr is GPL-3.0-or-later. Compatible: Rockbox, libmad and FAAD2 (GPLv2+); libopus, Tremor, libFLAC and kiss_fft (BSD); EchoPod and Podium (GPL-3.0+); echo-mini-newfw (MIT). Not usable: Helix MP3/AAC (RPSL/RCSL); ReChord (built on a leaked Rockchip SDK and prebuilt codec libraries, by its own inventory); RECHO (no public source, so ideas only).

## Ranked list

Sorted by expected gain over effort. Effort: S is under a day, M is a few days, L is longer. Owner: battery `perf/battery-2`, hw `perf/hw-accel`, usb `perf/usb-throughput`, dec (decoders, unowned), scan (library scan).

| # | Technique | Source | Gnarlyr today | Expected gain | Effort / risk | Owner |
|---|---|---|---|---|---|---|
| 1 | libmad `FPM_ARM`: synth and IMDCT accumulate in SMLAL and scale once per output | libmad `mad.h` (`FPM_ARM`, Thumb-aware) | `FPM_64BIT`, a full rounded 64-bit multiply per tap (`platform/device/logic/CMakeLists.txt:5`) | 25–35 % off MP3 decode on core 1 | S / low; keep `FPM_64BIT` for the host build | dec |
| 2 | libopus SMULL macros: define `OPUS_ARM_INLINE_ASM` (armv4 headers, valid Thumb-2); optionally `OPUS_FAST_INT64` on ARMv7-M | libopus 1.6.1 `celt/arm/fixed_armv4.h`, `kiss_fft_armv4.h`, `silk/arm/macros_armv4.h`, `SigProc_FIX_armv4.h` | Headers are vendored in `third_party/opus` but not enabled (`logic/opus.cmake`); `celt/arch.h:124` gives `OPUS_FAST_INT64 0` | 5–12 % of Opus cycles, smaller code. Opus is the tightest format, about 41–50 MIPS stereo (`firmware/docs/opus.md`) | S / low; bit-exact with C. Don't use the armv5e headers | dec |
| 3 | FAAD2 inline asm (`MUL_R`/`MUL_C`/`ComplexMult` via SMULL/SMLAL) | `third_party/faad2/fixed.h:159-232` | Disabled by `FAAD_NO_ARM_ASM` (`logic/CMakeLists.txt:53`); the asm is valid Thumb-2 | 5–10 % of AAC | S / low–medium; check the ±3 LSB reference test | dec |
| 4 | Screen-off playback at 96 MHz for light streams (FLAC/WAV/ALAC/MP3 ≤ 48 kHz, flat EQ, no resampling), switching only at a track boundary. Rockbox does the same with a refcounted `cpu_boost` for buffer fills and busy UI | EchoPod 5ca5e460; Rockbox `firmware/system.c:111-147`, `apps/buffering.c:1585-1596` | 200 MHz whenever audio is up, coasting to 100 MHz between passes (`src/app/app.c:273-279`, `board/clock_switch.c:183-195`) | Largest remaining playback saving: core 0 bus and PLL at about half (est. several mA) | M / medium: a clock switch with core 1 and DMA running; core 1's divider comes from the same PLL | battery |
| 5 | Shorter splash: 300 ms fade and 200 ms hold | EchoPod 8ced2183 | 600 + 500 ms with input held (`src/app/app.c:185`; unchanged on `perf/fast-boot`) | 0.6 s to a usable UI | S / none | boot (`perf/fast-boot`) |
| 6 | Core-1 clock governor: change the core 1 divider (CLKSEL0 bits 14:12) while it runs, keeping load near 75 %. echo-mini-newfw measured FLAC 16/44 at 9–12 MHz and MP3 at 20–25 MHz of core B | echo-mini-newfw `ETUDE.md` §24, §107 | Divider fixed at power-up, capped at 100 MHz (`logic/core1_control.c:29,45-51`) | Lower core 1 clock tree power (est. small to modest); compounds with items 1–3 | M / medium: underruns if it lags; uses the existing core 1 load figure | battery |
| 7 | Core 1 sleeps until there's work: SEV/WFE or a mailbox interrupt, or a SysTick armed only while playing at the time the DMA takes to free 4 KiB | EchoPod 92cc7a55; echo-mini-newfw §107 | WFI with a periodic tick: 1 ms (`core1/startup.c:8`), 5 ms on `perf/battery-2` | Fewer core-1 wakes (up to 20×) | S / low; echo-mini-newfw hit a hang when a wait outlived the tick | battery |
| 8 | Display DMA: DMAC1 channel 0, request 14, `VOP_CON` 0x16, `FIFO_WM` 0x808, CTL `0x0210d924`, no `VOP_START`, about 20 µs after the channel stops, odd widths as one-pixel lines. echo-mini-newfw measured 10 text lines 18.6 → 7.3 ms and Now Playing 13.4 → 7.8 ms | echo-mini-newfw §132–136 (MIT); Rockbox rk27xx `lcdif-rk27xx.c` does the same with linked-list items per row | CPU writes each pixel and checks busy every 4 (`logic/vop.c:59-69`); stock has no panel DMA | About 40 % off a frame push; overlap needs a second strip buffer (about 22 KB) | M / medium: byte order and FIFO-drain timing were found by trial on the Echo Mini | hw |
| 9 | Incremental rescan: reuse each track's tags when volume, first cluster, size, date and codec match the old index. echo-mini-newfw measured 18,003 tracks in 23.4 s vs 191 s for a full build | echo-mini-newfw §126; Rockbox `apps/tagcache.c:2267-2340`; ReChord "Update index" (idea only) | The change check is a fingerprint (`music_fingerprint`), but every tag is read again after a change; only covers are reused (`src/app/library_rescan_job.c:201-202`) | About 8× faster rescans after small edits (their figure) | M / medium: index identity rules; the builder must stay byte-identical | scan |
| 10 | Read tags in a fields-only mode that skips Ogg/Opus picture comments. echo-mini-newfw measured Opus 166 → 26 ms per file | echo-mini-newfw 0.102b4 | Not checked | Faster scans of Ogg/Opus libraries with embedded art | S / low | scan |
| 11 | Per-use clock gating as stock and rk27xx do: VOP while the panel sleeps, eMMC bus/card clocks between bursts, I2C per transaction, ADC | stock `0x03052688`; Rockbox `sd-rk27xx.c`, `lcdif-rk27xx.c lcdif_clock` | Four gate words at boot (`board/cru.c:34-43`). The VOP gate is turned on (`board/panel_vop.c:75`) and never off; eMMC gates on at `board/emmc.c:66-67` and stay on. The card clock already stops when idle (`board/mmc.c:57`, CLKENA bit 16) | Under 1 mA each (est.) | S–M / low–medium: wake latency, unnamed dependencies | battery |
| 12 | Longer screen-off playback passes, or core 0 woken by a DMAC block interrupt on one or two of the seven loop items | Rockbox low-watermark buffering (`apps/playback.c:1094-1164`); low-power DMA ping-pong practice | 10 ms passes with the screen off on `perf/battery-2`; the ring holds 16,384 frames (about 370 ms); DMA interrupts masked (`logic/dma_loop.c`) | 4–8× fewer main-loop wakes during screen-off playback | S (passes) / M (interrupt); the stall watch and `tick_clock` assume periodic passes | battery |
| 13 | FLAC CPU paths for when the accelerator self-test fails: a 32-bit order-specialised LPC when `bps + precision + log2(order) ≤ 32`; a fixed predictor as a running-difference recurrence; a word-wise bitreader refill (load and REV) | libFLAC `lpc.c`, `bitreader.c`; Rockbox `libffmpegFLAC/decoder.c:229-262,353-392` | 64-bit generic LPC (`src/codec/flac.c:357-368`); per-sample order switch (`:334-355`); byte refill (`src/codec/flac_bits.c:96-103`). The LPC accelerator covers order ≥ 3 when it passes (`logic/flac_accel.c`) | About 2× on the fallback LPC; 1–2 MHz from the refill | S–M / low | dec |
| 14 | USB DAC: correct drift on one packet in eight and count DAC frame gaps modulo 2048 | EchoPod a1c1b597 | Corrects every packet while outside the window (`logic/stream_drift.c:12-21`) | Avoids an audible 2 % pitch step | S / low | usb |
| 15 | DWC2 internal DMA with double-buffered mass storage (64 KB ping-pong, so the card write overlaps the next receive) | Rockbox `firmware/drivers/usb-designware.c`, `usbstack/usb_storage.c:56-86,596-613` | Polled; multi-packet bulk transfers on `perf/usb-throughput` (c17bd282) | 2–3× copy speed (est.; check GHWCFG2 for internal DMA first) | M–L / medium: USB overlay size | usb |
| 16 | eMMC/SD at 48–52 MHz high speed (CMD6) and the DW-MMC internal DMA (IDMAC) instead of a polled FIFO | DW-MMC databook; no Echo project does it yet. Rockbox rk27xx stays at 25 MHz because high speed broke writes on its controller | 24 MHz, polled FIFO (`board/mmc.c:154-163`, `logic/block_read.c`) | Up to 2× card throughput for rescans and USB copies (est.) | M / medium: card compatibility; check writes | usb |
| 17 | HIFIACC FFT for the Vorbis and AAC IMDCT; HIFIACC ALAC predictor; MP3 SYNTH accelerator. echo-mini-newfw measured −34 % on Vorbis, −9 % on AAC and −42 % on MP3 | echo-mini-newfw `docs/HIFIACC.md`, `docs/MP3ACC.md` (MIT) | Only FLAC LPC is accelerated | Large for Vorbis and MP3 | M / medium: not bit-exact (within 1 LSB on 87–94 % of outputs); MP3 SYNTH outputs 16-bit only | dec / hw |
| 18 | Rockbox's prime-factor FFT for the CELT backward MDCT, which removes the inter-stage twiddles | Rockbox `lib/rbcodec/codecs/libopus/celt/pfa.c`, `pfa_tables.h` (kiss_fft BSD terms) | Not done | 5–10 % of Opus (est.; Rockbox calls it its largest Opus win) | M / low: port from 2019 libopus to 1.6.1; the Opus bank already holds 99 of 128 KB | dec |
| 19 | Dirty regions in column bands as well as rows, so a clock or progress change sends part of a row | Rockbox skin engine per-viewport refresh (`apps/gui/skin_engine/skin_render.c`) | Row hashes only (`src/gfx/frame_diff.c`) | 2–4× smaller small updates (est.) | S–M / low; about 4 KB of hashes for four bands | hw |
| 20 | Draw glyphs by visiting set bits with CLZ and lay out a line once per strip. echo-mini-newfw measured 12.7 → 6.6 ms per 10 lines | echo-mini-newfw §136 | Not checked | Faster text-heavy frames | S–M / low | hw |
| 21 | Check the CS43198 Class H (adaptive supply) setting against DS1156F2 | Cirrus Class H parts adapt the amplifier rail to the signal | No Class H register is written during init (`logic/dac_sequence.c`), so the chip default applies | Possibly several mA at low volume (unknown) | S to check / low | battery |
| 22 | Tremor `_ARM_ASSEM_`, XPROD/MULT macros only | `third_party/tremor/asm_arm.h`; Rockbox `lib/asm_arm.h` | Not defined (`logic/tremor.cmake:17`) | 5–10 % of the Vorbis MDCT | S–M / low: `CLIP_TO_15` and `lsp_loop_asm` need IT blocks | dec |
| 23 | Async USB DAC: a feedback endpoint with a PI controller on ring fill, UAC2 at high speed. echo-mini-newfw measured bit-perfect playback with no dropped or repeated frames in 760k packets on Linux | echo-mini-newfw `docs/USB_AUDIO.md` | UAC1 full speed, adaptive, polled | Bit-perfect desktop playback | L / medium: host quirks; phones ignore the feedback | usb |
| 24 | Turn the Bluetooth module off after 30 s without a connection while its screen is closed | stock `0x030676a8` | Stays on | Module idle current | S / low | battery |
| 25 | Try `-O3`, `-funroll-loops` and LTO on the core-1 codec libraries, measured by emulator instruction count | — | `-O2`, no LTO on core 1 | 0–10 % (unknown) | S to try / code growth | dec |
| 26 | ALAC order-4/8 specialised adaptive FIR | Rockbox `libalac/alac.c` | Generic loops (`src/codec/alac.c:280-315`) | About 30 % of the ALAC predictor | S–M / low | dec |
| 27 | Retained standby: RAM kept, woken by the Power GPIO, SysTick off | Rockbox and phone practice; design in `firmware/docs/battery-measurements.md` on `perf/battery-2` | Design only | Instant-on with off-like drain, if the wake route works | L / high: wake route, watchdog, RTC accounting | battery |

## Already done or not applicable

- **Done in Gnarlyr:**
  - Pause standby with core 1 and the DAC powered down.
  - Panel sleep-in and display-off.
  - WFI between passes, with the core clock halved while waiting.
  - Drawing on change, with row dirty regions.
  - A circular, interrupt-free audio DMA list (`logic/dma_loop.c`), which beats rk27xx's interrupt per 256 samples.
  - The FLAC LPC accelerator.
  - The DW-MMC low-power card clock.
- **Done on `perf/battery-2`:**
  - The 24 MHz crystal with the PLL off in screen-off standby.
  - A 5 ms tick when resting.
  - Bluetooth drained in SysTick.
  - Refill hysteresis at half the ring.
  - No render work while dark.
  - A battery-trace kit, matching Rockbox's `battery_bench` approach of a minute sample logged without extra disk wakes.
- **Not applicable:**
  - IRAM/ICODE placement: there is no slower memory.
  - Rockbox's rk27xx edge-triggered wake workaround: the M3's WFI is level-sensitive.
  - dircache: the PC indexer and fixed records make it unnecessary.
  - Running CELT at a lower internal rate: its MDCT stays full size.
  - EchoPod 060aca4c: it fixes its own 16,384-sample FLAC blocks, which Gnarlyr doesn't use.
- **Ruled out:**
  - Helix decoders: the licence.
  - minimp3: floating point.
  - Tremor `_LOW_ACCURACY_`: precision.
  - Rockbox's ARM-state libmad and FLAC assembly: it uses RSC and PC-relative jump tables, so it would need a full Thumb-2 rewrite.
  - Q2 Pod's ideas: the Shanling Q2 runs Linux on an Ingenic X2000.
  - A 200 MHz row with a 1.2 GHz VCO (EchoPod 79881292): it lowers jitter, but echo-mini-newfw reads the TRM as saying it draws slightly more current.

## Projects surveyed

- [Rockbox](https://github.com/Rockbox/rockbox), master f17046ee. There is no RKnano port; `utils/rknanoutils` only unpacks images. The wiki pages wouldn't load for this survey.
- EchoPod (`echopod` remote, `main` c9ff9bd3, 0.12.0); Podium 4.5.9 source. Every power feature listed in Podium's notes is already in Gnarlyr.
- [echo-mini-newfw](https://github.com/berettavexee/echo-mini-newfw), MIT: the most detailed hardware notes for this SoC family (display DMA, the accelerators, the core-B governor, async UAC2). Its author reports 15.5 h and 14.6 h battery runs on an Echo Mini, mostly with the screen off.
- RECHO (atzkey, 0.10.1): the author claims 20+ h of playback, a 100 ms boot, accelerated rendering and a load-following clock. There's no source to check these against.
- ReChord, NeoPod, echopod-enchanced, [q2-pod](https://github.com/DiamondBond/q2-pod). Polorsk wasn't found.
- Codecs: libmad, libopus 1.6.1, FAAD2, Tremor and Tremolo, libFLAC, dr_flac and FFmpeg's flacdec, minimp3, Helix.
