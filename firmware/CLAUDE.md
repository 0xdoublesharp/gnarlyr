# FreshBase

FreshBase is firmware for the full-size Snowsky (FiiO) Echo player (480×222). It started from the Podium 4.5.9 source archive, which is a fork of EchoPod, the Echo Mini firmware (`UPSTREAM.md`, `CREDITS.md`). The same portable C app runs in a desktop simulator and on the player. The Echo is the only board.

FreshBase 1.0.0 is not released and has not run on a player. The repository and device rules are in `../AGENTS.md`.

## Identity

`identity.json` is the single source for `name` ("FreshBase": the USB product name and the start-up splash), `short_name` ("FreshBase" too: the Home title, and the splash where the name is too wide), `manufacturer` ("Doublesharp": the USB manufacturer name), `id` ("FRSHBS": the image brand, padded with zero bytes to its 8 bytes, and the USB storage vendor, padded with spaces to its 8 characters), the version, `data_directory` (`.freshbase`), `update_file` (`FRSHBS.IMG`) and `playlist_start_directive` (`FRESHBASE-START`). The build, the C code and the tools read it. Never write a name, the identifier or the version anywhere else in code or tests; change the file. C reads the `FIRMWARE_*` definitions, the firmware's Python tools read `tools/identity.py`, and the root tools read `echo_debug.IDENTITY`. The `no_identity_literals` test (`tools/check_identity_literals.py`) and the root test `tests/test_identity_literals.py` fail when a source file spells one of these names, or a former identifier listed in `former_ids`, outside `identity.json` and the generated fixtures. In documents, write FreshBase.

The firmware was called Doublesharp Echo (`DBLSHARP`, `/.doublesharp/`, `DBLSHARP.IMG`, `#DOUBLESHARP-START:`) until it was renamed; nothing reads those names any more (`docs/release-notes.md`). Doublesharp is still the maker: the four-byte file markers that start with `DS` are format tags named for the maker and did not change. Leave the old name where a document records what happened under it.

## Layout

| Path | Contents |
|---|---|
| `src/` | Portable app, freestanding C11, layered: `core` → `gfx`/`image`/`codec` → `fonts`/`ui`/`library`/`playback`/`settings` → `app`. `hal/` holds hardware interfaces only |
| `platform/sim` | SDL2 simulator: implements `hal`, owns `main()` |
| `platform/device/board` | Device-only drivers (RKnano Cortex-M3, DW-MMC eMMC/SD, DWC2 USB, the Cirrus DACs, Bluetooth UART), startup, linker scripts, the overlay loader |
| `platform/device/logic` | Driver logic without hardware access, unit-tested on the host |
| `platform/device/core1` | Decoder programs for the second core, stored in the core 0 image |
| `assets/` | Font sources and `lang/en.txt`, the English text and string catalogue |
| `tools/` | Python: image builder (`make_image.py`), size report (`size_report.py`), language pack builder (`lang_pack.py`), emulator (`echo_emulator`), PC library indexer, `echo_sync.py`, font and fixture generators |
| `tests/` | One test executable per module, emulator scenarios, fixtures |
| `docs/` | The design record: `architecture.md` (layers, rules, device platform), `hardware.md`, `library-format.md`, `playback-store.md` with `stored-order.md`, `session-journal.md` and `mixtapes.md`, `device-install.md`, `device-recovery.md`, `known-issues.md`, `user-guide.md`, `release-notes.md` |

The layer table in `docs/architecture.md` defines which layer may depend on which; keep to it.

## Build and test

Run the wrapper from the repository root. It routes `firmware/build` and `firmware/.venv` to the Scratch volume and stops if Scratch is not mounted. Prerequisites: `../README.md`.

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
| `device` | Cross-compiled `build/device/FRSHBS.IMG` with the developer rows in Settings → About |
| `release` | `build/release/FRSHBS.IMG` with the short About menu |

`test` also builds the `device` preset, runs the emulator scenarios on its image and checks formatting, the absence of comments, generated files being current and the Cortex-M3 RAM budget (`target_audit`).

### Emulator scenarios

Each scenario in `tests/emulator/test_echo_image.py` is its own test, named `device_image_boot_<test_name>`, so they run side by side. Use them this way:

- While editing, run only the scenarios the change touches: `python3 tools/firmware.py test --jobs 6 --regex 'device_image_boot_test_usb'`.
- Run the whole suite once, after the last edit, with `--jobs 6` (`--jobs 10` when nothing else is building). It takes about two minutes. Never run `test_echo_image.py` whole in one process: that is the slow, serial path.
- A branch must contain `perf(emulator): enter hooked functions through block hooks` and `perf(emulator): rest one slice at each idle key poll`. Start new work from the current `fresh-base` tip; if a branch is older, bring those commits in before running scenarios.
- A new scenario waits on events (a hooked function, a watched write, a drawn frame), not on a count of emulated instructions, and replaces any function it only needs to get past through a `CORE_SEAM`.

## Rules

- No comments in any code (C, CMake, Python); the `no_comments` test enforces it. Prose goes into `docs/`.
- No heap, no recursion, no VLAs, no mutable globals in `src/` outside `app.c`. Buffers are static and sized at compile time. `src/` includes only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`.
- Names: `PascalCase` types, `snake_case` functions prefixed with the module (`key_tracker_update`), `UPPER_SNAKE_CASE` prefixed constants, files named after the module.
- Functions do one thing, aim for under 20 lines, take at most 4 parameters (group the rest into a struct).
- Formatting is `.clang-format`; warnings are errors.
- Every rule in `core`, `gfx` and `ui` has a unit test named after the behaviour.
- Text a user reads is never a literal in code. Give it a name in `assets/lang/en.txt`, regenerate (`docs/architecture.md`, Languages) and read it with `i18n_text` or `i18n_format`; the `no_text_literals` and `test_pseudo_language` tests enforce it, and `tools/literal_allowlist.txt` lists the few literals that stay, each with its reason. A new screen's text must also pass `test_english_layout`.
- Documentation states only what this project checked. A result reported by Podium's or EchoPod's authors is either attributed to them or left out.

## Code space

`tools/size_report.py` prints used and free bytes against each limit after every image build, reading the limits from the linker maps; the linker scripts enforce them. It fails the image build when a library object keeps variables in both the main program and the USB overlay. The `echo_size_report` build target prints the full report (bytes per object, per library, biggest functions and tables). Release build with the text in the language pack:

| Limit | Used | Of | Free |
|---|---|---|---|
| Code window | 127,040 | 157,292 | 30,252 |
| Static RAM | 257,476 | 287,296 | 29,820 |
| Rescan overlay | 88,404 | 98,304 | 9,900 |
| USB overlay code | 12,852 | 65,536 | 52,684 |
| Module views (code + BSS) | 8,956 | 56,320 | 47,364 |
| Module settings (code + BSS) | 9,780 | 56,320 | 46,540 |
| Module library (code + BSS) | 4,076 | 56,320 | 52,244 |
| Decoder workspace | 41,644 | 41,984 | 340 |
| Language pack (English) | 4,613 | 8,192 | 3,579 |

- Keep at least 8 KB free in the release code window after each stage of the plan. Read the report after any change to device code and update these numbers and the ones in `docs/architecture.md` when they move.
- The core 0 link uses link-time optimisation. Libraries shared with core 1 or the rescan overlay stay out of it.
- `CORE_SEAM` (`src/core/include/core/seam.h`) marks the functions the emulator replaces or watches, so link-time optimisation cannot inline or remove them. It must stay on every function the emulator names (`tools/echo_emulator`, `tests/emulator`); the emulator fails when a named function is missing from the image.
- USB session code (mass storage, USB DAC, the DWC2 transfer code) and the updater live in the USB overlay, which is loaded into high RAM once a host is found. New code for those paths goes into the overlay, not the main program. Functions the overlay calls in the main program must be listed in `ECHO_OVERLAY_IMPORTS` (`platform/device/CMakeLists.txt`). Only listening for a host, the state kept across sessions and the start-up loader request stay resident. `docs/architecture.md` (Overlay loader, USB overlay) has the details.

## Screen design

Every screen should be thoughtful, elegant and pleasant to use. In practice:

- A screen is designed for all three text sizes and both orientations before it lands, and checked at the panel's real size in the simulator. Nothing is cut off, overlaps or sits unaligned at any of the six.
- One visual language: the theme's colours, the shared spacing and row heights, the shared icon set. No one-off sizes or colours in a screen.
- The primary thing is obvious and reachable with the fewest presses; secondary actions are discoverable on the screen itself, not hidden behind an undocumented hold.
- Every action gives feedback, every list has a considered empty state, and every wait says what is happening.
- Text is short, plain and consistent in wording across screens.
- Motion is brief and never delays input.

A screen change comes with simulator captures at the affected sizes and orientations for review.

## Porting from Podium and EchoPod

- Where the sources solve the same problem, keep one implementation and drop the others. One mechanism per problem.
- A ported piece should end up better than its source: smaller, faster, safer or more capable. Record the measured gain when there is one.
- A feature lands with its tests and inside the size budget: at least 8 KB free in the release code window, checked with the size report.
- No compatibility code for file names or formats that only earlier experiments wrote.
- Keep the original author's commit message on a cherry-picked commit, and credit the source in the message of a hand port.

## Generated files

Never edit by hand; regenerate and commit the output:
- `src/core/include/core/strings.h`, `src/core/strings_english.h` - `tools/lang_pack.py` from `assets/lang/en.txt`
- `src/fonts/` and `src/fonts/packs/` - `tools/font_build.py`, `tools/glyph_pack.py`
- `src/library/sort_fold_table.{c,h}` - `tools/fold_table.py`
- `tests/fixtures/small_library.db`, `small_covers.bin` - the PC indexer and cover exporter

Exact commands are in `docs/architecture.md`, Generated code. Python 3.14 reproduces the Unicode tables.

## Player safety

- **Never access the player without the owner's approval for that action.** Each install, each maskrom session and each raw read or write needs its own approval; an earlier approval does not carry over. See `../AGENTS.md`. Building, the simulator and the emulator need no approval and never touch USB.
- Writes to internal storage are restricted to the user volume and the two firmware slots (`platform/device/logic/emmc_layout`); never widen that.
- A broken image can leave the player unable to start; recovery goes through maskrom (`docs/device-recovery.md`). Have the exact-model recovery ready before any install.
- Installing builds on a player: `docs/device-install.md`.

## Talking to the player

Only after the owner approved it for this session.

- The owner enters maskrom (every key except Power held, then the USB cable plugged in). Ask, then wait.
- `rkdeveloptool-echo` talks to maskrom (USB `2207:262d`): upstream rkdeveloptool patched by `tools/patch_rkdeveloptool.py`. `docs/device-recovery.md` gives the build (the CMake project in `../tools/recovery`, output on Scratch). Two seconds after `ld` lists the device, `td` must print `Test Device OK.` before any `rl` or `wl`; `rd` restarts the player.
- Firmware slots on the full-size Echo: sector 0 and 81920, 10240 sectors (the image's first 5 MiB) each. Write slot 2 first, read every write back and compare before the next write:

  ```sh
  head -c 5242880 FRSHBS.IMG > slot.bin
  rkdeveloptool-echo wl 81920 slot.bin && rkdeveloptool-echo rl 81920 10240 rb.bin && cmp slot.bin rb.bin
  ```

- Slot files, read-backs and logs go under `/Volumes/Scratch/SnowskyEcho`, never in the repo. The original device backup stays under `.local/device-backups` in the main checkout.
- Internal storage mounts as `/Volumes/ECHO`. An update is copied with `cp -X` (no `._` files) to the drive's top level as `FRSHBS.IMG`, then `diskutil eject /Volumes/ECHO`; the updater runs once the USB session ends. This route is not yet proven on a player for FreshBase (`docs/known-issues.md`).

## Releasing

- The version is in `identity.json`. Nothing is released until the release checks in `../AGENTS.md` pass on the player and the owner approves.
- Add the version's entry to `docs/release-notes.md`: what changed, in plain words, and what was checked on the player.
