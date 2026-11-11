# Gnarlyr

Gnarlyr is firmware for the full-size Snowsky (FiiO) Echo player (480×222). It started from the Podium 4.5.9 source archive, which is a fork of EchoPod, the Echo Mini firmware (`UPSTREAM.md`, `CREDITS.md`). The same portable C app runs in a desktop simulator and on the player. The Echo is the only board.

Gnarlyr 1.0.0 is not released. The repository and device rules are in `../AGENTS.md`.

## Identity

`identity.json` is the single source for `name` ("Gnarlyr": the USB product name and the start-up splash), `short_name` ("Gnarlyr" too: the Home title, and the splash where the name is too wide), `manufacturer` ("Doublesharp": the USB manufacturer name), `id` ("GNARLYR": the image brand, padded with zero bytes to its 8 bytes, and the USB storage vendor, padded with spaces to its 8 characters), the version, `data_directory` (`.gnarlyr`), `update_file` (`GNARLYR.IMG`) and `playlist_start_directive` (`GNARLYR-START`). The build, the C code and the tools read it. Never write a name, the identifier or the version anywhere else in code or tests; change the file. C reads the `FIRMWARE_*` definitions, the firmware's Python tools read `tools/identity.py`, and the root tools read `echo_debug.IDENTITY`. The `no_identity_literals` test (`tools/check_identity_literals.py`) and the root test `tests/test_identity_literals.py` fail when a source file spells one of these names, or a former identifier listed in `former_ids`, outside `identity.json` and the generated fixtures. In documents, write Gnarlyr.

The four-byte file markers beginning with `DS` remain maker format tags. Settings and library layouts are unchanged; the version-1 cassette catalogue and session layouts are described in `docs/mixtapes.md` and `docs/session-journal.md`. The canonical identifier, data folder, update filename and playlist directive now follow the Gnarlyr identity. Moving an installed developer build to these identifiers requires the prepared update/data transition; preserve its settings and saved music state before the first canonical boot.

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
| `docs/` | The design record: `architecture.md` (layers, rules, device platform), `ogg-vorbis.md` and `opus.md`, `hardware.md`, `library-format.md`, `smart-lists.md`, `playback-store.md` with `stored-order.md`, `session-journal.md` and `mixtapes.md`, `device-install.md`, `device-recovery.md`, `known-issues.md`, `user-guide.md`, `release-notes.md` |

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
| `device` | Cross-compiled `build/device/GNARLYR.IMG` with the developer rows in Settings → About |
| `release` | `build/release/GNARLYR.IMG` with the short About menu |

`test` also builds the `device` preset, runs the emulator scenarios on its image and checks formatting, the absence of comments, generated files being current and the Cortex-M3 RAM budget (`target_audit`).

### Emulator scenarios

Each scenario in `tests/emulator/test_echo_image.py` is its own test, named `device_image_boot_<test_name>`, so they run side by side. Use them this way:

- While editing, run only the scenarios the change touches: `python3 tools/firmware.py test --jobs 6 --regex 'device_image_boot_test_usb'`.
- Run the whole suite once, after the last edit, with `--jobs 6` (`--jobs 10` when nothing else is building). It takes about two minutes. Never run `test_echo_image.py` whole in one process: that is the slow, serial path.
- A branch must contain `perf(emulator): enter hooked functions through block hooks` and `perf(emulator): rest one slice at each idle key poll`. Start new work from the current `gnarlyr` tip; if a branch is older, bring those commits in before running scenarios.
- A new scenario waits on events (a hooked function, a watched write, a drawn frame), not on a count of emulated instructions, and replaces any function it only needs to get past through a `CORE_SEAM`.

## Rules

- No comments in any code (C, CMake, Python); the `no_comments` test enforces it. Prose goes into `docs/`.
- No heap, no recursion or VLAs. Resident mutable app state belongs in `app.c`. A cold module may keep a private, fixed-size work buffer in its own BSS; cancel its jobs before eviction and never keep pointers into that buffer in resident playback state. Buffers are sized at compile time. `src/` includes only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`.
- Names: `PascalCase` types, `snake_case` functions prefixed with the module (`key_tracker_update`), `UPPER_SNAKE_CASE` prefixed constants, files named after the module.
- Functions do one thing, aim for under 20 lines, take at most 4 parameters (group the rest into a struct).
- Formatting is `.clang-format`; warnings are errors.
- Every rule in `core`, `gfx` and `ui` has a unit test named after the behaviour.
- Text a user reads is never a literal in code. Give it a name in `assets/lang/en.txt`, regenerate (`docs/architecture.md`, Languages) and read it with `i18n_text` or `i18n_format`; the `no_text_literals` and `test_pseudo_language` tests enforce it, and `tools/literal_allowlist.txt` lists the few literals that stay, each with its reason. A new screen's text must also pass `test_english_layout`.
- Documentation states only what this project checked. A result reported by Podium's or EchoPod's authors is either attributed to them or left out.

## Code space

`tools/size_report.py` prints used and free bytes against each limit after every image build, reading the limits from the linker maps; the linker scripts enforce them. It fails the image build when a library object keeps variables in both the main program and the USB overlay. The `echo_size_report` build target prints the full report (bytes per object, per library, biggest functions and tables). Release build with the text in the language pack, except the rows marked `device` preset:

| Limit | Used | Of | Free |
|---|---|---|---|
| Code window | 151,136 | 157,292 | 6,156 |
| Code window, `device` preset | 155,464 | 157,292 | 1,828 |
| Static RAM | 267,588 | 278,528 | 10,940 |
| Rescan overlay (code + BSS) | 95,128 | 98,304 | 3,176 |
| USB overlay code | 12,012 | 65,536 | 53,524 |
| Module views (code + BSS) | 27,732 | 44,032 | 16,300 |
| Module settings (code + BSS) | 22,100 | 44,032 | 21,932 |
| Module library (code + BSS) | 39,936 | 44,032 | 4,096 |
| Module listening (code + BSS) | 32,828 | 44,032 | 11,204 |
| Audio processing module (code, no BSS) | 2,752 | 12,288 | 9,536 |
| Audio DSP module (code, no BSS) | 5,928 | 8,192 | 2,264 |
| Ogg file module, Vorbis and Opus (code, no BSS) | 6,656 | 8,192 | 1,536 |
| Extension module (code, no BSS) | 3,524 | 8,192 | 4,668 |
| Extension module, `device` preset | 7,552 | 8,192 | 640 |
| Decoder workspace | 41,648 | 41,984 | 336 |
| Language pack (English) | 9,364 | 16,384 | 7,020 |
| Vorbis program image bank | 64,840 | 130,048 | 65,208 |
| Vorbis core-1 static state | 32,952 | 36,864 | 3,912 |
| Opus program image bank | 98,928 | 131,072 | 32,144 |
| Opus core-1 static state | 30,908 | 36,864 | 5,956 |

- Keep at least 4 KB free in the release code window and at least 1 KB in the `device` one. The `device` build is what the debug loop installs, so it must link; the free space is a margin for the next change, not a hardware need. Read the report after any change to device code and update these numbers and the ones in `docs/architecture.md` when they move.
- Code that runs only from one module's screens belongs in that module, debug-loop code that only a running script needs belongs in the extension module, and so does code that must stay callable beside every UI module but that start-up, USB sessions and the audio path never use, behind wrappers that do nothing when the module is missing (`docs/architecture.md`, Demand-loaded UI modules and Extension module). Hooks the debug loop needs at start-up, before a script or while another module is loaded stay resident.
- The extension module occupies the 8 KiB window at `0x0304A000` in both presets. It contains wait motion, Home artwork and file removal; the `device` build uses the same module and window for debug steps, probes, RTC reads and writes and screenshots. It loads after `board_power_hold()` through CRC and running-code checks with slot fallback. If both copies fail, resident wrappers omit optional drawing, refuse file removal and abort debug scripts while startup, playback and USB remain available.
- The rescan overlay owns the scan callbacks, cover pass, timing and temporary file state. Its private BSS follows its code and is cleared by the overlay loader; both must fit its window. Resident code keeps preflight checks, loading and result messages.
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
- A feature lands with its tests and inside the size budget: at least 4 KB free in the release code window and 1 KB in the `device` one, checked with the size report.
- No compatibility code for file names or formats that only earlier experiments wrote.
- Keep the original author's commit message on a cherry-picked commit, and credit the source in the message of a hand port.

## Generated files

Never edit by hand; regenerate and commit the output:
- `src/core/include/core/strings.h`, `src/core/strings_english.h` - `tools/lang_pack.py` from `assets/lang/en.txt`
- `platform/device/logic/audio_resample_table.h` - `tools/audio_resample_table.py`
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
  head -c 5242880 GNARLYR.IMG > slot.bin
  rkdeveloptool-echo wl 81920 slot.bin && rkdeveloptool-echo rl 81920 10240 rb.bin && cmp slot.bin rb.bin
  ```

- Slot files, read-backs and logs go under `/Volumes/Scratch/SnowskyEcho`, never in the repo. The original device backup stays under `.local/device-backups` in the main checkout.
- Internal storage mounts as `/Volumes/ECHO`. An update is copied with `cp -X` (no `._` files) to the drive's top level as `GNARLYR.IMG`, then `diskutil eject /Volumes/ECHO`; the updater runs once the USB session ends. File-copy installs with automatic restart/reconnect are recorded on the owner's player through earlier Gnarlyr developer builds. The canonical identifier transition still needs its own device validation (`docs/known-issues.md`).

## Releasing

- The version is in `identity.json`. Nothing is released until the release checks in `../AGENTS.md` pass on the player and the owner approves.
- Add the version's entry to `docs/release-notes.md`: what changed, in plain words, and what was checked on the player.
