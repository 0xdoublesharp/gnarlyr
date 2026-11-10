# FiiO stock firmware reference: ECHO 2.0.0

What has been established about FiiO's stock firmware for the full-size SNOWSKY ECHO by static analysis. It is the reference for values the Gnarlyr board code copies from stock, and for the analysis tools in `tools/`.

Everything here comes from reading the image, not from running it. Names describe what the code does; they are not FiiO's symbols. An address is a Thumb instruction address in the loaded image.

## The image

| Property | Value |
|---|---|
| File | `ECHOV200.IMG`, 39,845,892 bytes, from FiiO's [ECHO firmware page](https://forum.fiio.com/note/showNoteContent.do?id=202602051101498161611&tid=17) |
| SHA-256 | `c6a3b6c86b141c0a428771f8a611fbd44cc12d561acb033a98640b0bf24f8d57` |
| Rockchip CRC-32 | `0xd571ad35` |
| Container markers | `Rockchip`, `RKnano SDK 1.0`, `RKnanoFW` |
| Module table | 91 rows, 71 with code |
| Main code | file offset `0x297cc`, loads at `0x03050000`, 178,416 bytes |
| Main initialised data | loads at `0x03004000`, 2,652 bytes |
| Main BSS | `0x03004a5c`, 130,616 bytes |
| Vector | stack `0x03004000`, entry `0x030622ca` |

Many module rows share execution addresses, because stock loads modules over each other at run time. They cannot all be mapped into one flat address space; an analysis has to pick compatible modules. Rows with a zero execution address are resources, not code.

The container layout and CRC follow the [RKnano format](https://github.com/maaraneasi/nanitecfw/blob/main/tools/rknano_fw.py), checked against this image.

## Tools

- `tools/echo_image.py` validates the CRC and module bounds and exports chosen, non-overlapping modules as an ELF that preserves the original bytes and load addresses. The ELF is for analysis only and must never be flashed.
- `tools/run_analysis.py` imports that ELF into Ghidra with the `ARM:LE:32:Cortex` language and exports pseudocode and instructions. `tools/check_analysis.py` compares what Ghidra loaded with the export map.
- `tools/rea_session.mjs` keeps one [REA](https://github.com/morluto/rea) server and database alive between queries.
- Ghidra 12.1.4 is the version REA accepts. Its macOS arm64 decompiler has to be built from the sources in the release (`make ghidra_opt ARCH_TYPE='-arch arm64' OSDIR=mac_arm_64`).

Run `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests` after changing the parser or exporter. Images, ELF files, Ghidra projects and reports stay on the Scratch volume.

## General-purpose pins and ADC

| Address | What it does |
|---|---|
| `0x030563e2` | Reads one pin: register offset `0x50` of the selected bank. Bank bases are `0x40160000`, `0x40170000` and `0x50030000` |
| `0x03056388`, `0x030563c2` | Set or clear one pin's bit at register offsets 4 and 0 of a bank: direction and output |
| `0x030527d4` | Pin function select, through `0x50010000` |
| `0x03061bee` | Key scan: checks bank 2 pin 5 (Power) first, otherwise samples ADC channel 1 and maps it to keys |
| `0x0307e8c0` | Module 1 entry: enables the ADC block at `0x400d0000` (sets bit 3 of `0x400d0008`) |
| `0x0305ff84` | ADC shutdown: clears control bits at `0x400d0008` and stops its clocks |
| `0x0307f060` | Battery: samples ADC channel 0, scales by 5000/1024, checks thresholds and can shut down |

Key ladder bands on ADC channel 1, as unsigned readings: 0–69, 70–183, 200–319, 320–409 and 450–549. The firmware's `platform/device/logic/key_ladder.c` uses the same bands for Volume Down, Volume Up, Play, Menu and Back. Readings in the gaps are not keys.

## Display

The display code is in module 4: file offset `0x59b18`, loads at `0x0307e48c`, 1,076 bytes.

- Start-up (`0x0307e730`) drives the reset pin, bank 2 pin 15, high, low, high with waits of 50, 150 and 30 ms, selects the LCD controller's command mode, plays the command table at `0x03004140`, and switches to pixel mode.
- All 24 commands and their data in that table, including both gamma tables and memory access control `0x28`, match `platform/device/logic/panel.c`. Sleep-out waits 120 ms in both.
- The LCD controller is at `0x60070000`: control at offset `0x00`, set-up word `0x1021` at `0x08`, commands at `0x28`, data at `0x2c`.
- The window routine (`0x0307e50e`) sends inclusive column bounds, adds 49 to the row bounds, then starts command `0x2c`.
- `0x03051c90` changes the clock registers and re-runs the controller set-up, so display timing follows the system clock.

## DAC

- Start-up is the long write sequence in stock function `0x03051736`: power `0x020052 = 2`, `0x0f0010 = 0xe7`, `0x020000 = 0xf6`, the `0x040010`–`0x040019` block, four mono tables from `0x180009`, then release of the serial-port and headphone power bits. `platform/device/logic/dac_sequence.c` mirrors it.
- Stock waits 12 ms after releasing the output power bit. The firmware does the same.
- The register writer (`0x03060f00`) sends three address bytes, an auto-increment byte and the value to bus addresses `0x60` and `0x66` (7-bit `0x30` and `0x33`). It sets the channel map registers `0x05000a` and `0x05000b` to 7 and 15, and applies balance to the volume registers `0x090001` and `0x090002`.
- The reader is `0x0306101c`, using the same addressing for both chips.
- The parts are two CS43198, as FiiO lists. On the owner's ECHO (4 GB) both chips report ID `43 19 80`, revision `A1`, subrevision `06` (Doublesharp Echo 1.0.0, Settings → About, read 2026-11-09). Some inherited code names still say CS43131; the register sequence is stock's either way.
- Not compared with stock yet: the default digital filter, the volume curve, the gain values and the power-down order.

## Limits of this record

- REA sometimes reports a function as several separate pieces joined by a branch to shared code. A numeric address range is not proof that everything inside it belongs to one function.
- Pin roles, battery calibration and left/right channel identity are read from code and still need a check on the player.
- Only modules 0, 1 and 4 have been examined.
