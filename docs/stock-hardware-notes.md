# Stock hardware notes: ECHO 2.0.0

What FiiO's stock firmware for the full-size ECHO does with the hardware, compared with Gnarlyr. It extends [stock-firmware.md](stock-firmware.md), which covers the image, the tools, the key ladder bands, the display start-up and the DAC start-up sequence. USB and the eMMC/SD controllers are covered elsewhere and left out here.

Everything comes from static analysis of `ECHOV200.IMG` (SHA-256 `c6a3b6c8…8d57`). Addresses are Thumb instruction addresses in the loaded image; `DAT_` values are literal-pool words read from the image. Names describe behaviour and are not FiiO's symbols. Nothing here was measured on a player unless it says so. Gnarlyr references are `firmware/` paths at `integration/next` 6ff88b36.

## Method

- Every module of the image was exported (`tools/echo_image.py`), disassembled with `arm-none-eabi-objdump -M force-thumb` and indexed by the peripheral base words its literal pools load. Main (module 0) holds nearly all hardware code. Module 1 holds ADC start-up and update checks, module 4 the panel, module 9 USB DAC mode, module 33 the audio player.
- Main, with modules 1, 4 and 9, was decompiled with Ghidra 12.1.4 (`ARM:LE:32:Cortex`), seeded with every BL target and Thumb function pointer: 730 functions.
- Pin helpers: `0x030527d4` (bank, pin, function) selects the pin function; `0x03056388` (bank, pin, 1 = output) sets direction; `0x030563c2` (bank, pin, level) drives; `0x030563e2` (bank, pin) reads; `0x03061410` (bank, pin, mode) sets the pull.
- Generated listings and decompiles stay on Scratch under `/Volumes/Scratch/SnowskyEcho/reports/stock-hardware/`.

## I2C devices

The I2C start routine `0x030604c8` (bus, 8-bit address, kHz) has these callers:

| Address | Bus | Caller | Device |
|---|---|---|---|
| `0x60`, `0x66` | 0 | `0x03060f00`, `0x0306101c` | the two CS43198 |
| `0xA2` | 1 | `0x03061882`–`0x03061960` | PCF8563-class clock chip |
| `0x38` | 3 | `0x03052e00` | SDK codec code with 16-bit registers; nothing calls it |
| `0x20` | 0 | overlay module 62 | SDK FM tuner (band limits 650/760/870); not used on the ECHO |

There is no charger or fuel-gauge IC on I2C.

## Battery and charging

| Item | Stock | Evidence |
|---|---|---|
| VBUS present | GRF `0x500100E8` bit 1 | `0x03053668`: `return (*0x500100e8 & 3) >> 1` |
| Charge complete | GPIO2.3 input, no pull; read at most every 500 ms; one high reading latches "full" until the cable goes | `0x03065c80` |
| Charge enable | GPIO0.14 output; on/off only | reset handler `0x030622ca`; USB DAC `0x03081536` |
| USB power path | GPIO2.0 output; on/off only | same |
| Charge hooks | `0x030617de`, `0x030617e0` are empty (`bx lr`), called with 1 at charge start (`0x03065c58`) and 0 at stop (`0x03065ce4`) | |
| Current select, capacity | none: no pin, no register, no mAh constant | |

- The reset handler drives GPIO0.14 and GPIO2.0 high unconditionally. Power-off (`0x030617f2`) drives both low, waits 5 ms, then releases the hold pin GPIO2.6.
- USB DAC mode (`0x03081536`, module 9) chooses the pair from settings byte `+0x357`: 0 → both low, 1 → GPIO2.0 high only, 2 → both high. So GPIO2.0 is the power path from USB and GPIO0.14 enables charging; there is no third level.
- Computer port versus wall charger: no BC1.2. On VBUS, stock starts the DWC2 core (`0x03068144`: clocks, IRQ 0x15 → `0x030680a8`) and watches GINTSTS. A USB suspend (`0x800`) followed by a USB reset (`0x1000`) marks a host. `0x03068212` reports a charger once 300 ms pass without that, and `0x03066bb8` enters USB mode only for a host.
- Gauge: ADC channel 0 × 5000 / 1024 (`0x03065c4e`), ten samples averaged at start. The icon has five levels from the table at data `0x030047FE`: 3450, 3480, 3550, 3650 and 3830 mV; above 3830 mV is full. A level changes only after 16 consecutive samples agree (`0x03065dea`). More than 20 samples below 3450 mV post message `0x1E`, the shutdown. While charging the icon only animates, one step every 50 ticks. It shows full at GPIO2.3 or above 4249 mV (`0x03065d92`).

Gnarlyr: VBUS is read from the same bit (`platform/device/board/usb.c:41`). The supply pins follow the cable rather than staying high (`platform/device/board/power.c:55`, `main.c:183`); `hardware.md` records why: with the pins held high and no cable, a host doesn't see the player. Full needs GPIO2.3 high for 2 s, at least 4050 mV and a 5 s settle (`platform/device/logic/battery_gauge.c:148`), which is stricter than stock's single reading. The 11-point percentage curve (`battery_gauge.c:12`) is finer than stock's five levels and shares stock's 3450 mV floor.

A charging-rate display can't come from firmware facts: the current is neither sensed nor selectable, and the capacity isn't recorded. A time-to-full estimate from the slope of the cell voltage while charging is possible. A milliamp figure would also need the capacity from the cell label or FiiO's specification.

## Audio path

Register addresses are CS43198 addresses; the datasheet is DS1156F2.

**Register writer** `0x03060f00`:
- Every write goes to 0x60 and then 0x66 on I2C0 at 300 kHz, as three address bytes, `0x01` (auto-increment) and the value. The reader `0x0306101c` runs at 400 kHz.
- `0x05000A`/`0x05000B` are rewritten per chip: 7 on 0x60, 0x0F on 0x66.
- Balance (setting `+0x359`, centre 12, 0.5 dB a step) is added to one chip's volume registers; a mute (0xFF) bypasses it.
- After two failed writes a chip is skipped until the next restart.

**Restart** `0x03051b22`: `0B0000=1F`, then 80 ms, then GPIO1.4 (amplifier) high, then init `0x03051736`. The init order matches `platform/device/logic/dac_sequence.c`, which [stock-firmware.md](stock-firmware.md) records. Gnarlyr differs in two places: it writes muted volumes during init where stock writes 0 dB (`dac_sequence.c:51-52`), and it writes the sample-rate byte after the outputs are released (`:225`).

**Gain** `080000`:
- Stock writes `0x31` for High and `0x10` for Low (`0x03051b18`: `movs r1,#16; lsls r0,r1,#15`), the same as `dac_sequence.c:34-35`.
- OUT_FS = 01, which `0x10` selects, is listed as reserved in DS1156F2 §7.5.1; `0x20` would be the documented 1.41 Vrms setting. Whether stock's Low matches either documented level needs a measurement on a player.
- At runtime `0x03061206` rewrites the register while the output is powered, with no PDN_HP step or mute, although §7.5.1 allows OUT_FS changes only with PDN_HP set.

**Digital filter** `090000`, from setting `+0x354`, changed at run time by `0x030611c8` without a power-down:

| Setting | Value | Filter |
|---|---|---|
| 0 (default) | `0x02` | fast roll-off, low latency, high-pass on |
| 1 | `0x42` | fast, phase-compensated |
| 2 | `0x82` | slow, low latency |
| 3 | `0xC2` | slow, phase-compensated |
| 4 | `0x22` | NOS |

Gnarlyr fixes `0x02` (`dac_sequence.c:50`). EchoPod has the same five values.

**Ramp and mute:**
- `090003 = 0xEC` sets ramp-down, PCM_VOL_BEQA, soft ramp (SZC = 10) and auto-mute. With BEQA set, PCM_VOLUME_A (`090002`) controls both channels and `090001` is ignored (DS1156F2 §4.2, volume control). Stock and Gnarlyr both still write both registers.
- Stock changes the volume in one write and leaves the step to the chip's 1 dB/ms ramp. Gnarlyr also ramps in software, 3 dB every 2 ms (`dac_sequence.c:33`, `board/dac.c:98`).

**Stop** (module 33 `0x0308e70a`, then `0x030516f4`):
1. Stop message, then 200 ms.
2. GPIO2.1 (mute) high, GPIO2.14 high.
3. `020000 = 0x10`, a plain write that sets PDN_HP but powers the other blocks up.
4. 5 ms, then `0B0000 = 00`, then 10 ms, then GPIO1.4 low.

Stock never mutes the PCM volume first and never waits for PDN_DONE. Gnarlyr's `dac_output_off` (`dac_sequence.c:298`) mutes first and waits.

**Start** (module 33 near `0x0308d896`):
1. GPIO2.14 high, GPIO2.1 high, GPIO1.4 low, GPIO2.2 high, with waits of 2, 1 and 8 ms.
2. Restart.
3. I2S1 transmit and DMA on.

The unmute (GPIO2.1 low) comes from the jack handler after a 60-tick debounce. A cold start has 160 ms of fixed waits in stock and in Gnarlyr (`dac_sequence.c:197-213`).

**Clocks and DMA:**
- The sample-rate byte (`0x030610d2`) and the ACODEC PLL rows (tables `0x0306f130`, `0x0306f1c0`, `0x0306f250`) match `audio_math.c:17-25` and `board/dac.c:288-302`, except 32 kHz. Stock takes 32 kHz from its 8 kHz family (8.192 MHz MCLK); Gnarlyr takes it from the 48 kHz family (12.288 MHz).
- Stock caches the last row and skips reprogramming when it hasn't changed.
- Both use DMAC channel 5 with CFG `0x840`/`0x1007` and control `0x00909124` to `0x40030024`. Stock re-arms per block from the completion interrupt (`0x030660fa`); Gnarlyr runs a circular linked list (`dma_loop.c:4`, `0x18909124`).

**Volume curve** `0x03061262(level)` writes the same value to both volume registers, in 0.5 dB units:

| Route | 1 | 2 | 3 | 4 | 5 | 6 | n ≥ 7 |
|---|---|---|---|---|---|---|---|
| 3.5 mm | 200 | 170 | 150 | 130 | 122 | 117 | 120 − n |
| 4.4 mm | 200 | 177 | 157 | 137 | 125 | 117 | 120 − n |
| Bluetooth | 225 | 199 | 175 | 150 | 137 | 130 | 130 − n |

- Levels run 0–120, and level 0 is mute (0xFF).
- Wired level 120 is 0 dB; Bluetooth tops out at −5 dB.
- The route flag comes from GPIO2.8 with a 60-tick debounce (module 33 `0x0308de1e`).

Gnarlyr has 60 steps of 1 dB from 0 dB down to −59 dB, with 5 dB extra attenuation on 4.4 mm (`dac_sequence.c:31`, `:255-263`), which stock doesn't apply above level 6.

## Bluetooth module

The code is in main (frames, report parser, routing, volume swap), in overlay 84 (the Bluetooth screen: power, scan, pair and connect) and in overlay 33 (the play-state notices). UART1 `0x400B0000` comes from the base table at `0x0306f118` (UART0/1/2 at `0x400A0000`/`0x400B0000`/`0x400C0000`) and the generic driver: open `0x03051380`, read `0x030566a0`, write `0x03056708`.

Frames are 5 bytes, `'C' x '0' y 0`, built at `0x030672ae`/`0x030672ca` and sent by message `0x1C9`:

| Frame | Use |
|---|---|
| `CS00` / `CP00` | Start / stop scan |
| `CL06` + 6-byte address; `CH06` + address | Connect; `CH` retries; the remembered address is at settings `+0x238` |
| `CT00` | Disconnect |
| `CQ00` | Status: every 100 ms unconnected, every 500 ms connected (`0x03067304`) |
| `CY0` `0x1E` / `CY0` `0x00` | Stream on at play (message `0x1D4`) / off at stop or pause (`0x1D3`) |
| `C 0x86 00` / `C 0x87 00` | Play / pause notices, sent only when the module reported version `CV15` (`0x030222c4` = 5) |

Reports:

| Report | Meaning |
|---|---|
| `CV1<v>` | Ready, with the module version |
| `CS<nn><address><name>`, `CSEN` | Scan result; end of scan |
| `CLOK`/`CLER`, `CHOK`/`CHER` | Connect result |
| `CQO<nn><address><name>` | Connected |
| `CQS`, `CQL`, `CQI`, `CT` | Not connected |
| `CP`/`CC` | Pause/play from the headphones |
| `CA`/`CB` | Previous/next |
| `CKKK` | Ignored |

No other command exists: no volume command, no absolute volume and no AVRCP metadata.

**Power and routing:**
- GPIO2.7, 2.16 and 2.17 are outputs, low at boot (`0x03053234`). They go low again after 30 s with Bluetooth on and nothing connected (`0x030676a8`).
- Routing to the module is GPIO2.1 high and GPIO2.14 low, and the reverse on disconnect. Stock changes no I2S, CRU or GRF settings and doesn't power the DAC down.
- I2S1 is opened the same way for every route: 24-bit in 32-bit slots, pins GPIO1.0–1.3 on function 1, and no second I2S port.
- So the module most likely samples the DAC's analog output. This is an inference to be confirmed on a player.

**Volume:**
- Settings hold separate levels: current `+0x12`, wired `+0x13` (default 30), USB DAC `+0x14` (default 40) and Bluetooth `+0x15` (default 100).
- `0x03063e68` swaps the Bluetooth level in when routed, and `0x03061262` applies the Bluetooth row of the curve above.
- The default Bluetooth level is −15 dB at the DAC.

Gnarlyr:
- It sends the same frames and `CY` values (`logic/bt_module.c:13`, `:565`, `:573`) and routes with the same pins (`board/bluetooth.c:148`).
- It sends play/pause notices whatever the version (`bt_module.c:209` stores it).
- On first routing it also writes EchoPod's GRF and CRU list (`bluetooth.c:137`), which stock's routing path doesn't.
- It keeps a separate Bluetooth level, 0–60 like the wired one, and applies it only while audio is routed to the module (`board/audio.c`, `hal_audio_set_bluetooth_volume`). Its curve, `DAC_CURVE_BLUETOOTH`, puts level 60 at −5 dB, the top of stock's Bluetooth row, and the default 50 at −15 dB, stock's default. While routed, the balanced offset, the headphone ceiling and the channel trims don't apply. Earlier builds reused the wired level, which played 10–25 dB below stock's Bluetooth default.

## Display

- **Controller access.** Stock reaches the VOP (`0x60070000`) only through small accessors in main: control `+0x00` (`0x0306e928`; `0x800` command mode, `2` pixel mode), command `+0x28` (`0x0306eaf0`) and data `+0x2c` (`0x0306eade`). Only overlay module 4 calls them.
- **Drawing.** Module 4 provides sleep in/out, the window (`0x0307e50e`), a full clear, a single-window fill (`0x0307e832`) and a rectangle blit (`0x0307e682`). The blit opens one window per row and makes one function call per pixel.
- **No panel DMA.** Stock's DMA users are UART, I2S1 and a generic copy.
- **Timing word.** `0x03053e18` writes the constant `0x1021` to `+0x08` at every clock change (`0x03051c90`), so it doesn't follow the system clock.
- **Backlight.** PWM channel 0 (`0x400F0000`; channels 4 and up at `0x40100000`), 0x10 apart: counter, period, duty, control (`0x00010003` runs).
  - On (`0x03065a82`): ungate clock 0x90, pulse reset 0x17, 25 kHz, no fade. Off (`0x03061514`): gate 0x90, GPIO2.4 low.
  - Brightness table `0x030047D6`: {70, 58, 45, 30, 5, 95} as PWM low time, that is 30, 42, 55, 70, 95 and 5 % on; stock never drives 100 %.
  - Screen-off times `0x030047DC`: {15, 30, 60, 180, 300, 1800} s.

Gnarlyr:
- It writes pixels in an inline loop that checks busy every 4 pixels (`logic/vop.c:59-69`), which is cheaper than stock's call per pixel.
- It derives the timing word from the clock (`vop.c:27-33`, re-applied at `board/panel_vop.c:131-136`).
- Its backlight has levels of 20–100 % (`logic/backlight.c:7-8`) and a ramp.

## Input

- **Tick and scan.** SysTick is 5 ms at boot (`0x03060ecc(5)` in the reset handler) and 10 ms after every clock change (`0x03068316`). The key job is a SysTick timer entry with period 2 (data `0x030046e0`, callback `0x03061dbe`, dispatcher `0x03067fd4`), so keys are evaluated every 20 ms. The ADC order is {1, 1, 2, 0} (`0x030046c0`).
- **Decode** `0x03061bee`. Power (GPIO2.5) is read first and masks the ladder. Stock decodes the lowest reading seen from press until release (above 1000), with the bands in [stock-firmware.md](stock-firmware.md); a reading in a gap leaves the key unchanged.
- **Two ladder keys** aren't distinguishable: no band or code path stands for a combination. A series ladder reads the lower key. With one resistor per key, combinations would land on other keys' bands (Menu + Back about 266, the Play band). Which applies needs a raw reading on a player. Power plus one ladder key is the only combination the hardware separates, and stock never uses it.
- **Timing** `0x03061c96`, in 20 ms scans:
  - A press counts on the 2nd scan.
  - Long press is 20 scans (400 ms); repeat every 4 scans after scan 24.
  - Holding Power about 1.04 s sends the power-off message `0x1D` (`0x030670c0`).
- **Screen-off gestures** (`0x03061e00`): single volume taps act after 410 ms, double taps skip tracks, and a long Menu toggles the play mode.

Gnarlyr:
- It uses the same bands (`logic/key_ladder.c:11-14`) and merges Power into the key mask (`board/input.c:51-56`), so Play + Power is a sound combination.
- It scans every 5 ms on a 1 ms SysTick (`board/clock.c:14-15`, `input.c:106-117`).
- A press needs 4 samples and a release 2 (`key_scanner.h:10-11`).
- Long press is 600 ms, the first repeat 400 ms, then every 120 ms (`src/app/app.c:79-82`).

## Clocks and power

**Profiles.** Stock keeps 36 clock profiles at `0x0307B500`: an id, then PLL, system, hclk, pclk, calc core and calc bus rates in Hz.
- `0x030687c0(n)` activates a profile and `0x0306880c(n)` releases it, in a 64-bit mask at `0x03022DF8`.
- `0x030685dc` sums each column over the active profiles. The result is never below profile 1 (96 MHz) and never above profile 35.
- `0x03068316` chooses the PLL and dividers, and `0x03051c90` programs them in the same order as `platform/device/board/clock_switch.c:82`, with the lock wait on CON1 bit 10 done with interrupts off.
- GRF `+0xC4` gets `0x3040` from a 180 MHz system clock and `0xC000` from a 300 MHz calc clock.
- There is no voltage scaling.

These rates come from running stock's solver in an emulator with each profile set active, not from the player:

| Use | Profiles | PLL | System | Calc | pclk |
|---|---|---|---|---|---|
| Boot | 2 | 96 | 96 | – | 48 |
| UI (module 1) | 3 | 196 | 196 | – | 49 |
| Playback up to 48 kHz, UI on | 3 + 4 + 23 | 600 | 200 | 85.7 | 50 |
| Hi-res, middle class | 3 + 4 + 24 | 600 | 200 | 120 | 50 |
| Hi-res, high class | 3 + 4 + 25 | 1400 | 200 | 200 | 50 |
| USB (`0x03066e32`) | 31 | 400 | 200 | 400 | 50 |
| Settings flush (`0x0306dfc4`) | 30 | 200 | 200 | – | 50 |
| Library update, browsing | 35 | 1440 | 205.7 | 480 | 51 |

- **Decoder class.** Module 33 picks it at `0x0308f05a` from the sample rate and a stream value that appears to be the bitrate. Class 23 covers 48 kHz and 2304 kbit/s and below; class 25 covers the largest streams.
- **Profile 3 is never released.** No call releases it, so once the UI is up core 0 stays at 196–200 MHz.
- **Profile row values.** The 96 MHz row has the same CON0/CON1 as `platform/device/logic/pll.c:45`. Stock's 196 MHz row uses a 588 MHz VCO; Gnarlyr's 200 MHz row uses 400 MHz (`pll.c:46`).

**Gnarlyr's policy**:
- Core 0 runs at 96 MHz when idle and 200 MHz with the screen on or audio playing (`src/app/app.c:273-279`), and coasts at /2 between passes (`clock_switch.c:183`, `board/main.c:253`).
- It sleeps in WFI between passes (`main.c:243`).
- Core 1 runs at 100 MHz, or 200 MHz for DSD and hi-res Vorbis.

That is at or below stock's clocks in every mode except those two decoder cases.

**Core 1 power-down** (`0x0306e3b8`): gates `0x30`–`0x38`, `0x40`–`0x43`, `0x48`–`0x4A`, `0x4C`, `0x02` and `0x92` off, then sets PMU `+0x0C` bit 1. Gnarlyr does the same and also gates `0x39` and `0x4B` (`platform/device/logic/core1_control.c:194`).

**Clock gates.** At boot, module 1 (`0x0307ea86`) writes a complete gate image:
- `0x80`–`0xA4`: `7FFF1FA4`, `FFFFCF9F`, `00070007`, `03FF03FF`, `1FFF1FFF`, `07EF0606`, `FFFFEC00`, `FFFF4000`, `FFFFFCFE`, `01FF0004`.
- Resets `0xC0`–`0xCC`: `FFFFE67F`, `FFFFFE32`, `03FF00FF`, `00FF00D4`.

From then on stock switches clocks per use (`0x03052688(id, on)`):

| Gate | Use |
|---|---|
| `0x6C` | MMC card clock, around each transfer |
| `0x83`–`0x85` | I2C, per transaction |
| `0x0A`, `0x93` | ADC |
| `0x07`, `0x6A` | USB, while in use |
| `0x7F` | VOP |
| `0x90`, `0x91` | PWM |
| `0x8F` | Watchdog |
| `0x0B`, `0x0C`, `0x8E` | Timer `0x400E0000` |
| `0x0E`, `0x10`, `0x12`–`0x14`, `0x16`, `0x81`, `0x82`, `0x96` | Audio (`0x03055ece`) |

Gnarlyr writes four words after the splash (`platform/device/board/cru.c:34`, `main.c:296`). That leaves on clocks stock keeps off: ids `05` and `08`–`0C`; `10`–`13`, `17`–`1B` and `1F`; `82`, `83`, `86`, `87` and `8F`; and all of `0x94`–`0x9C`.

**Tick and watchdog.**
- SysTick runs from the core clock with a 5 ms period at boot (`0x03060ecc(5)`) and 10 ms after every clock change (`0x03068316`). Gnarlyr's period is 1 ms (`board/clock.c:14`).
- Stock's delays are busy loops (`0x03065a54`).
- The watchdog control is `0x16` then `|1`, TORR `0x0D`, kick `0x76` (`0x030616d8`). Gnarlyr uses control `0x15` (`board/watchdog.c:16`).

**Sleep and wake.**
- Stock's main loop busy-polls (`0x030689ea`). The only WFI (`0x03055e4c`) waits for a DMA transfer in the voice-prompt overlays 42–58.
- There is no standby state. Power-off (`0x030617f2(0)`) is GPIO0.14 low, GPIO2.0 low, 5 ms, then GPIO2.6 low.
- The player restarts from the power key or VBUS; `reset_handler` checks GRF `+0xE8` bit 1 and starts in its USB mode when a cable is present.
- Stock touches only the clock chip's control register 0 and time registers 2–8 (`0x030618d8`, `0x0306193e`), and sets no alarm or timer, so there is no clock-chip wake.

## Boot sequence

`reset_handler` `0x030622ca`:
1. Pin presets. Low: GPIO2.2, 2.7, 2.17, 2.16, 2.1. High: GPIO2.14, 0.14, 2.0.
2. `0x0307eb58`, then `0x0307ea86`: the gate and reset image, the interrupt vectors and the NVIC. SysTick at 5 ms. Profile 2 (96 MHz).
3. `0x0307ebd8` (module 1):
   - GPIO init `0x03053234`, the ADC, and the panel reset with 50, 150 and 30 ms waits plus 120 ms after sleep-out.
   - Battery check `0x0307f060`: 18 samples. Below 3400 mV with no USB, it waits 200 ms and powers off.
   - Profile 3.
   - With no USB, the splash `0x030546b4`: 200 ms, backlight on, 34 frames with 30 ms after each, then a 36 × 30 ms hold. That is about 2.3 s of fixed busy-waiting before frame decoding is counted.
4. `0x0307ec4e`: open `ECHOECHO.IMG`; when it is absent, try `ECHOV100.IMG` up to `ECHOV498.IMG` one name at a time, up to 399 root-directory lookups on every boot. Then load the library and settings (`0x03057c0a`, `0x0305d1d8`).
5. Message dispatch and release of profile 2.
6. Clock-chip read, with one retry; when the time is invalid, write 2026-01-03 12:00.
7. Watchdog, state restore, the jack pins (GPIO2.8, GPIO0.13), then the main loop.

The fixed costs in stock's roughly 9 s start are:
- the splash timing, about 2.3 s;
- the update-file name scan;
- about 350 ms of panel waits;
- the DAC's 80 ms waits.

The library load can't be timed statically. Gnarlyr keeps the panel timings, but its splash is a 600 ms fade and a 500 ms hold (`src/app/app.c:185`), and it checks one update name.

## Improvements

In priority order. Gains are estimates. Every item needs a check on the player before it can be called working.

| # | Change | Gain | Risk | Where |
|---|---|---|---|---|
| 1 | Separate Bluetooth volume, defaulting near stock's −15 dB, with stock's Bluetooth curve (top −5 dB), swapped in on routing | Fixes the quiet Bluetooth output, about 10–25 dB | Low; needs a settings field and a check that the module follows the DAC's analog level | `board/audio.c:589`, settings |
| 2 | Settle Low gain: stock writes `0x10` (reserved OUT_FS); the documented 1.41 Vrms value is `0x20`. Compare stock Low, `0x10` and `0x20` by ear or meter | Correct Low level | None in code | `dac_sequence.c:35` |
| 3 | 24 MHz crystal when the screen is off, audio idle and USB not listening; `run_from_crystal` exists | Largest remaining idle saving: PLL off, core clock a quarter of 96 MHz | Medium: UART baud, ADC timing, wake latency | `src/app/app.c:273` |
| 4 | Longer SysTick period (stock 10 ms) or a tickless sleep | Up to 10× fewer wake-ups in idle and standby; the base for instant-on standby | Medium: `tick_clock`, stall watch, input cadence | `board/clock.c:14` |
| 5 | Stock's boot gate image, minus the clocks Gnarlyr's drivers turn on themselves | Leakage of many idle blocks | Medium: an unnamed clock may be needed | `board/cru.c:34` |
| 6 | Gate the MMC card clock (`0x6C`) between transfers, as stock does | Small, constant during playback | Low–medium; MMC owner's area | `board/mmc.c` |
| 7 | I2C0 at 300 kHz for the DACs, as stock | About 50 ms less per DAC start; faster fades | Low; one constant | `board/dac.c:83` |
| 8 | Volume writes to `090002` only, with `090001` initialised to the same value | Half the I2C traffic per volume step | Low, but `090001` must not stay muted if BEQA is ever cleared | `dac_sequence.c:244` |
| 9 | User choice of stock's five DAC filters (EchoPod's `dac_set_filter`) | Feature parity with stock | Low | `dac_sequence.c:50` |
| 10 | Finer low end of the volume curve (stock reaches −100 dB at level 1 and has 0.5 dB steps near the top) | Usable low levels on sensitive earphones | Medium: visible behaviour, settings migration | `dac_sequence.c:255` |
| 11 | Owner's choice: drop the extra 5 dB on 4.4 mm, which stock doesn't apply | 5 dB more on balanced | Hearing-level decision | `dac_sequence.c:31` |
| 12 | Drop the second `0B0000=1F` and its 80 ms wait at DAC start | 80 ms per cold start | Medium: stock does it too; needs a click check | `dac_sequence.c:210` |
| 13 | Cache the ACODEC row and skip reprogramming when it hasn't changed, as stock does | At least 5 ms per start | Low–medium | `board/dac.c:288` |
| 14 | Send play/pause notices only after module version `5`, as stock does | Avoids frames other module firmware may not expect | Low; Bluetooth owner's area | `bt_module.c:209` |
| 15 | Turn the Bluetooth module off after 30 s without a connection when its screen is closed | Battery | Low | `board/bluetooth.c` |
| 16 | Shorter VOP write cycle, or less frequent busy checks | Up to about 25 % off a full-frame push | Pixel errors on some panels; needs the player | `logic/vop.c:27` |
| 17 | Time-to-full estimate from the charging voltage slope | Charging-rate display without a current sensor | Low; estimate only | `battery_gauge.c` |

Items 1 (separate Bluetooth volume and curve), 9 (Settings → DAC filter) and 17 (the charging rate and time-to-full estimate on the charge screen) have since been implemented; the rest are open. This document itself changed no firmware code. None of the items is both small and verifiable without the player: the ones with host-testable logic (1, 3, 8, 9, 14) change audible or power behaviour that only the player can confirm.
