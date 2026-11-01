# Podium

Custom firmware for the full-size Snowsky (FiiO) Echo player (480×222), forked from [EchoPod](https://github.com/BasilSkyWalk/echopod) 0.6.0 (remote `echopod`), the Echo Mini 8 GB firmware: an iPod-style UI with iPod-sized text, Cover Flow, an on-player music library, favorites, M3U playlists, an Up Next queue, Bluetooth and USB DAC mode. The same portable C app runs in a desktop simulator and on the player. Changes are not sent back to EchoPod.

The Echo is a board of the same code (`ECHO_BOARD=ECHO`): preset `dev-echo` for the simulator and tests, `device-echo` and `release-echo` for the player (`ECHOV498.IMG`, version 0.0.1, not yet run on hardware). The `device` and `release` presets still build the Echo Mini image. Many names in the code and docs still say EchoPod.

Hardware notes for the Echo port (stock Echo and Mini firmware and RECHO, decompiled) live outside the repo, in `~/Sync/Monkey/assets/20261031T113308.323001/ghidra/`.

## Layout

| Path | Contents |
|---|---|
| `src/` | Portable app, freestanding C11, layered: `core` → `gfx`/`image`/`codec` → `fonts`/`ui`/`library`/`playback`/`settings` → `app`. `hal/` holds hardware interfaces only |
| `platform/sim` | SDL2 simulator: implements `hal`, owns `main()` |
| `platform/device/board` | Device-only drivers (RKnano Cortex-M3, DW-MMC eMMC/SD, DWC2 USB, CS43131 DAC, Bluetooth UART), startup, linker script |
| `platform/device/logic` | Driver logic without hardware access, unit-tested on the host |
| `platform/device/core1` | Decoder program for the second core, linked into the core 0 image |
| `tools/` | Python: image builder (`make_image.py`), PC library indexer, `echo_sync.py`, font and fixture generators |
| `tests/` | One test executable per module, plus fixtures |
| `docs/` | The design record: `architecture.md` (layers, rules, device platform), `hardware.md`, `library-format.md`, `device-install.md`, `device-recovery.md`, `known-issues.md` |

The layer table in `docs/architecture.md` defines which layer may depend on which; keep to it.

## Build and test

```sh
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
cmake --preset dev && cmake --build --preset dev
ctest --preset dev                      # everything
ctest --preset dev -R <test_name>       # one test
./build/dev/platform/sim/echo_sim --card /path/to/music --system-dir build/system
```

Toolchain: cmake, SDL2, clang-format, arm-none-eabi-gcc and binutils.

| Preset | Output |
|---|---|
| `dev` | Host build of the simulator and tests, under UndefinedBehaviorSanitizer |
| `device` | Cross-compiled `build/device/HIFIEC90.IMG` with the full Diagnostics menu |
| `release` | `build/release/HIFIEC90.IMG` with the short Diagnostics menu |
| `dev-echo` | `dev` for the full-size Echo board (480×222); `ctest --preset dev-echo` |
| `device-echo` | Cross-compiled `build/device-echo/ECHOV498.IMG` for the full-size Echo (brand `PODIUM`, version 0.0.1) |
| `release-echo` | `build/release-echo/ECHOV498.IMG` with the short Diagnostics menu |

`ctest` also checks formatting, the absence of comments, generated files being current and the Cortex-M3 flash/RAM budget (`target_audit`).

## Rules

- No comments in any code (C, CMake, Python); the `no_comments` test enforces it. Prose goes into `docs/`.
- No heap, no recursion, no VLAs, no mutable globals in `src/` outside `app.c`. Buffers are static and sized at compile time. `src/` includes only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`.
- Names: `PascalCase` types, `snake_case` functions prefixed with the module (`key_tracker_update`), `UPPER_SNAKE_CASE` prefixed constants, files named after the module.
- Functions do one thing, aim for under 20 lines, take at most 4 parameters (group the rest into a struct).
- Formatting is `.clang-format`; warnings are errors.
- Every rule in `core`, `gfx` and `ui` has a unit test named after the behaviour.
- The device code window is nearly full; watch `target_audit` when adding device code.

## Generated files

Never edit by hand; regenerate and commit the output:
- `src/fonts/` and `src/fonts/packs/` - `tools/font_build.py`, `tools/glyph_pack.py`
- `src/library/sort_fold_table.{c,h}` - `tools/fold_table.py`
- `tests/fixtures/small_library.db`, `small_covers.bin` - the PC indexer and cover exporter

Exact commands are in `docs/architecture.md`, Generated code.

## Player safety

- Writes to internal storage are restricted to the user volume and the two firmware slots (`platform/device/logic/emmc_layout`); never widen that.
- A broken image can leave the player unable to start; recovery goes through maskrom (`docs/device-recovery.md`).
- Installing builds on a player: `docs/device-install.md`.
