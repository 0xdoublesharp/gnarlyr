# Doublesharp Echo

Doublesharp Echo (short name Doublesharp) is firmware for the full-size Snowsky (FiiO) Echo player (480×222). It started from the Podium 4.5.9 source archive, which is a fork of EchoPod, the Echo Mini firmware (`UPSTREAM.md`, `CREDITS.md`). The same portable C app runs in a desktop simulator and on the player. The Echo is the only board.

Doublesharp 1.0.0 is not released and has not run on a player. The plan, its stages and its rules are in `../docs/own-firmware-plan.md`; the repository rules are in `../AGENTS.md`.

## Identity

`identity.json` is the single source for `name` ("Doublesharp Echo": the USB product name and the start-up splash), `short_name` ("Doublesharp": the Home title), `id` ("DBLSHARP": the 8-character image brand and USB storage vendor) and the version. The build, the C code and the tools read it. Never write a name, the identifier or the version anywhere else in code; change the file. In documents, use "Doublesharp Echo" in titles and where the firmware is introduced, and "Doublesharp" after that.

Names that follow from it: the update file `DBLSHARP.IMG`, the data folder `/.doublesharp/`, file magics that start with `DS`, and the playlist directive `#DOUBLESHARP-START:`.

## Layout

| Path | Contents |
|---|---|
| `src/` | Portable app, freestanding C11, layered: `core` → `gfx`/`image`/`codec` → `fonts`/`ui`/`library`/`playback`/`settings` → `app`. `hal/` holds hardware interfaces only |
| `platform/sim` | SDL2 simulator: implements `hal`, owns `main()` |
| `platform/device/board` | Device-only drivers (RKnano Cortex-M3, DW-MMC eMMC/SD, DWC2 USB, CS43131 DAC, Bluetooth UART), startup, linker scripts, the overlay loader |
| `platform/device/logic` | Driver logic without hardware access, unit-tested on the host |
| `platform/device/core1` | Decoder programs for the second core, stored in the core 0 image |
| `tools/` | Python: image builder (`make_image.py`), size report (`size_report.py`), emulator (`echo_emulator`), PC library indexer, `echo_sync.py`, font and fixture generators |
| `tests/` | One test executable per module, emulator scenarios, fixtures |
| `docs/` | The design record: `architecture.md` (layers, rules, device platform), `hardware.md`, `library-format.md`, `device-install.md`, `device-recovery.md`, `known-issues.md`, `user-guide.md`, `release-notes.md` |

The layer table in `docs/architecture.md` defines which layer may depend on which; keep to it.

## Build and test

Run the wrapper from the repository root. It routes `firmware/build` and `firmware/.venv` to the Scratch volume and stops if Scratch is not mounted. Prerequisites: `../docs/building-firmware.md`.

```sh
python3 tools/firmware.py setup
python3 tools/firmware.py build --preset device     # or release
python3 tools/firmware.py test                      # everything
python3 tools/firmware.py test --regex <test_name>  # one test
python3 tools/firmware.py sim --card /path/to/music
```

| Preset | Output |
|---|---|
| `dev` | Host build of the simulator and tests, under UndefinedBehaviorSanitizer |
| `device` | Cross-compiled `build/device/DBLSHARP.IMG` with the developer rows in Settings → About |
| `release` | `build/release/DBLSHARP.IMG` with the short About menu |

`test` also builds the `device` preset, runs the emulator scenarios on its image and checks formatting, the absence of comments, generated files being current and the Cortex-M3 RAM budget (`target_audit`).

## Rules

- No comments in any code (C, CMake, Python); the `no_comments` test enforces it. Prose goes into `docs/`.
- No heap, no recursion, no VLAs, no mutable globals in `src/` outside `app.c`. Buffers are static and sized at compile time. `src/` includes only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`.
- Names: `PascalCase` types, `snake_case` functions prefixed with the module (`key_tracker_update`), `UPPER_SNAKE_CASE` prefixed constants, files named after the module.
- Functions do one thing, aim for under 20 lines, take at most 4 parameters (group the rest into a struct).
- Formatting is `.clang-format`; warnings are errors.
- Every rule in `core`, `gfx` and `ui` has a unit test named after the behaviour.
- Documentation states only what this project checked. A result reported by Podium's or EchoPod's authors is either attributed to them or left out.

## Code space

`tools/size_report.py` prints used and free bytes against each limit after every image build, reading the limits from the linker maps; the linker scripts enforce them. It fails the image build when a library object keeps variables in both the main program and the USB overlay. The `echo_size_report` build target prints the full report (bytes per object, per library, biggest functions and tables). Release build at commit `357e9ea`:

| Limit | Used | Of | Free |
|---|---|---|---|
| Code window | 137,896 | 157,292 | 19,396 |
| Static RAM | 264,048 | 287,296 | 23,248 |
| Rescan overlay | 88,404 | 98,304 | 9,900 |
| USB overlay code | 13,364 | 65,536 | 52,172 |

- Keep at least 8 KB free in the release code window after each stage of the plan. Read the report after any change to device code and update these numbers and the ones in `docs/architecture.md` when they move.
- The core 0 link uses link-time optimisation. Libraries shared with core 1 or the rescan overlay stay out of it.
- `CORE_SEAM` (`src/core/include/core/seam.h`) marks the functions the emulator replaces or watches, so link-time optimisation cannot inline or remove them. It must stay on every function the emulator names (`tools/echo_emulator`, `tests/emulator`); the emulator fails when a named function is missing from the image.
- USB session code (mass storage, USB DAC, the DWC2 transfer code) and the updater live in the USB overlay, which is loaded into high RAM once a host is found. New code for those paths goes into the overlay, not the main program. Functions the overlay calls in the main program must be listed in `ECHO_OVERLAY_IMPORTS` (`platform/device/CMakeLists.txt`). Only listening for a host, the state kept across sessions and the start-up loader request stay resident. `docs/architecture.md` (Overlay loader, USB overlay) has the details.

## Generated files

Never edit by hand; regenerate and commit the output:
- `src/fonts/` and `src/fonts/packs/` - `tools/font_build.py`, `tools/glyph_pack.py`
- `src/library/sort_fold_table.{c,h}` - `tools/fold_table.py`
- `tests/fixtures/small_library.db`, `small_covers.bin` - the PC indexer and cover exporter

Exact commands are in `docs/architecture.md`, Generated code. Python 3.14 reproduces the Unicode tables.

## Player safety

- **Never access the player without the owner's approval for that action.** Each install, each maskrom session and each raw read or write needs its own approval; an earlier approval does not carry over. See `../AGENTS.md` and `../docs/goal.md`. Building, the simulator and the emulator need no approval and never touch USB.
- Writes to internal storage are restricted to the user volume and the two firmware slots (`platform/device/logic/emmc_layout`); never widen that.
- A broken image can leave the player unable to start; recovery goes through maskrom (`docs/device-recovery.md`). Have the exact-model recovery ready before any install.
- Installing builds on a player: `docs/device-install.md`.

## Talking to the player

Only after the owner approved it for this session.

- The owner enters maskrom (every key except Power held, then the USB cable plugged in). Ask, then wait.
- `rkdeveloptool-echo` talks to maskrom (USB `2207:262d`): upstream rkdeveloptool patched by `tools/patch_rkdeveloptool.py`. `../docs/recovery-preparation.md` records how it is built on this machine (the CMake project in `../tools/recovery`, output on Scratch). Two seconds after `ld` lists the device, `td` must print `Test Device OK.` before any `rl` or `wl`; `rd` restarts the player.
- Firmware slots on the full-size Echo: sector 0 and 81920, 10240 sectors (the image's first 5 MiB) each. Write slot 2 first, read every write back and compare before the next write:

  ```sh
  head -c 5242880 DBLSHARP.IMG > slot.bin
  rkdeveloptool-echo wl 81920 slot.bin && rkdeveloptool-echo rl 81920 10240 rb.bin && cmp slot.bin rb.bin
  ```

- Slot files, read-backs and logs go under `/Volumes/Scratch/SnowskyEcho`, never in the repo. The original device backup stays under `.local/device-backups` in the main checkout.
- Internal storage mounts as `/Volumes/ECHO`. An update is copied with `cp -X` (no `._` files) to the drive's top level as `DBLSHARP.IMG`, then `diskutil eject /Volumes/ECHO`; the updater runs once the USB session ends. This route is not yet proven on a player for Doublesharp (`docs/known-issues.md`).

## Releasing

- The version is in `identity.json`. Nothing is released until the completion checks in `../docs/goal.md` pass on the player and the owner approves.
- Add the version's entry to `docs/release-notes.md`: what changed, in plain words, and what was checked on the player.
