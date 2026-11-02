# Podium

Custom firmware for the full-size Snowsky (FiiO) Echo player (480×222), forked from [EchoPod](https://github.com/BasilSkyWalk/echopod) 0.6.0 (remote `echopod`), the Echo Mini 8 GB firmware: an iPod-style UI with iPod-sized text, Cover Flow, an on-player music library, favorites, M3U playlists, an Up Next queue, Bluetooth and USB DAC mode. The same portable C app runs in a desktop simulator and on the player. Changes are not sent back to EchoPod.

The Echo is the only board: preset `dev` for the simulator and tests, `device` and `release` for the player (`ECHOV454.IMG` for version 4.5.4, named after the version as FiiO does; 4.5.3 runs on the player). The Echo Mini board of EchoPod is gone. Many names in the code and docs still say EchoPod.

Hardware notes for the Echo port (stock Echo and Mini firmware and RECHO, decompiled) live outside the repo, in `~/Sync/Monkey/assets/20261031T113308.323001/ghidra/`.

## Layout

| Path | Contents |
|---|---|
| `src/` | Portable app, freestanding C11, layered: `core` → `gfx`/`image`/`codec` → `fonts`/`ui`/`library`/`playback`/`settings` → `app`. `hal/` holds hardware interfaces only |
| `platform/sim` | SDL2 simulator: implements `hal`, owns `main()` |
| `platform/device/board` | Device-only drivers (RKnano Cortex-M3, DW-MMC eMMC/SD, DWC2 USB, CS43131 DAC, Bluetooth UART), startup, linker script |
| `platform/device/logic` | Driver logic without hardware access, unit-tested on the host |
| `platform/device/core1` | Decoder program for the second core, linked into the core 0 image |
| `site/` | Website: pages, photos and the `podium` Cloudflare Worker (`worker.js`, feedback form into D1 `podium-feedback`); `tools/build_site.py` builds it into `build/site/`, `cd site && npx wrangler@4 deploy` publishes it. Read feedback: `npx wrangler@4 d1 execute podium-feedback --remote --command "select * from feedback order by id desc"` |
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
| `device` | Cross-compiled `build/device/ECHOV454.IMG` for the full-size Echo (brand `PODIUM`, version from `PODIUM_VERSION`, 4.5.4) with the full Diagnostics menu |
| `release` | `build/release/ECHOV454.IMG` with the short Diagnostics menu |

`ctest` also builds the `device` preset, runs the emulator tests on its image and checks formatting, the absence of comments, generated files being current and the Cortex-M3 flash/RAM budget (`target_audit`).

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

## Talking to the player

- The user enters maskrom (every key except Power held, then the USB cable plugged in); ask, then wait for it with a background command that reports maskrom, the `ECHO` drive or the player on USB, whichever appears:

  ```sh
  for i in $(seq 1 200); do m=$(rkdeveloptool-echo ld 2>&1 | grep -i maskrom); e=$(ls /Volumes 2>/dev/null | grep -x ECHO); u=$(ioreg -p IOUSB -w0 2>/dev/null | grep -i -E 'ECHO|Podium|2207' | head -1); if [ -n "$m$e$u" ]; then echo "t=$((i*3))s maskrom=[$m] volume=[$e] usb=[$u]"; exit 0; fi; sleep 3; done; echo TIMEOUT-10min
  ```

- `rkdeveloptool-echo` talks to maskrom (USB `2207:262d`): upstream rkdeveloptool patched by `tools/patch_rkdeveloptool.py`, built in `~/.local/opt/rkdeveloptool-echo` and linked from `~/.local/bin`. To rebuild it, follow `docs/device-recovery.md`, step 1. Two seconds after `ld` lists it, `td` must print `Test Device OK.` before any `rl` or `wl`; `rd` restarts the player.
- Firmware slots on the full-size Echo: sector 0 and 81920, 10240 sectors (the image's first 5 MiB) each. Write slot 2 first, read every write back and compare:

  ```sh
  head -c 5242880 ECHOV454.IMG > slot.bin
  rkdeveloptool-echo wl 81920 slot.bin && rkdeveloptool-echo rl 81920 10240 rb.bin && cmp slot.bin rb.bin
  ```

- Backups and slot files go in the session's working directory, never in the repo.
- Running Podium shows up as the `/Volumes/ECHO` drive. Copy an update with `cp -X` (no `._` files) to the drive's root, then `diskutil eject /Volumes/ECHO`; the updater runs once the USB session ends.
