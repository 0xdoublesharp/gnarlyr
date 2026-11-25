# Gnarlyr

Gnarlyr replaces FiiO's firmware on the full-size SNOWSKY ECHO (480 × 222 display). It is an independent project. SNOWSKY, FiiO, and ECHO belong to their respective owners.

Version 1.0.0 is in development. The [known issues](docs/known-issues.md) record checks on the owner's 4 GB ECHO and the checks still needed for release.

## Where it comes from

Gnarlyr builds on [Podium](https://podium-echo.online) 4.5.9, open-source firmware for this player by another author. Podium is a fork of [EchoPod](https://github.com/BasilSkyWalk/echopod), the firmware for the Echo Mini. The interface, the library, playback, the updater, the Echo port and the equalizer are their authors' work. [`UPSTREAM.md`](UPSTREAM.md) records the exact starting point and what has been ported since; [`CREDITS.md`](CREDITS.md) lists everyone whose work is used.

## Firmware at a glance

The library combines internal storage and microSD when a readable card is present. Browse by cover or tag, play individual CUE tracks and audiobook chapters, and save queues as mixtapes. The player builds the library from tags and cover art. `tools/echo_sync.py` can build it on a computer.

Sound controls include graphic and parametric EQ, ReplayGain, crossfeed, and headphone profiles. USB serves both drives in storage mode. In DAC mode it offers USB Audio Class 2.0 at high speed and Class 1.0 at full speed. See the [USB DAC guide](docs/user-guide.md#using-the-player-as-a-usb-dac) for supported rates and bit depths.

The build, C source, and tools share [`identity.json`](identity.json) for the name, maker, image brand, version, data directory, update filename, and playlist directive.

## Documents

| Document | Contents |
|---|---|
| [`docs/user-guide.md`](docs/user-guide.md) | Using the player: music layout, buttons, settings, updating, going back to FiiO's firmware |
| [`docs/known-issues.md`](docs/known-issues.md) | Safety, what is unproven on a player, limits |
| [`docs/release-notes.md`](docs/release-notes.md) | Release notes and unreleased changes |
| [`docs/device-install.md`](docs/device-install.md) | Installing by file copy, later updates, and maskrom for a player that won't start |
| [`docs/device-recovery.md`](docs/device-recovery.md) | Writing FiiO's firmware back through maskrom |
| [`docs/architecture.md`](docs/architecture.md) | Layers, rules, device platform, tests |
| [`docs/hardware.md`](docs/hardware.md) | The hardware the drivers talk to |
| [`docs/library-format.md`](docs/library-format.md) | Library, covers, favorites and bookmark file formats |
| [`ROADMAP.md`](ROADMAP.md) | Feature references and remaining device checks |
| [`../AGENTS.md`](../AGENTS.md) | The repository and device rules |

## Build and test

Use the wrapper from the repository root. It checks the toolchain and keeps build output on the Scratch volume. See the [root README](../README.md#building-the-firmware) for prerequisites and the [development workflow](../docs/development.md) for test selection and integration.

```sh
python3 tools/firmware.py setup
python3 tools/firmware.py build --preset device
python3 tools/firmware.py build --preset release
python3 tools/firmware.py test --regex '^test_player$'
python3 tools/firmware.py sim --card /path/to/music
```

| Preset | Output |
|---|---|
| `dev` | Host build of the simulator and the tests, under UndefinedBehaviorSanitizer |
| `device` | Cross-compiled `build/device/GNARLYR.IMG` with the developer rows in Settings → About |
| `release` | `build/release/GNARLYR.IMG` with the short About menu |

The wrapper links `firmware/build` to its build directory under `/Volumes/Scratch/SnowskyEcho/build/`. Each image build reports used and free space in the code window, static RAM, and overlays.

With no selection, `test` runs the complete CTest suite. It includes native, simulator, Python, and ARM image checks, along with formatting, generated-file freshness, and the Cortex-M3 RAM audit.

## Simulator

The simulator reads music from the card directory and writes the library, covers, and saved state to its system directory. The wrapper supplies a system directory on Scratch.

| Device button | Keyboard |
|---|---|
| Volume up (moves menus down) | Down arrow |
| Volume down (moves menus up) | Up arrow |
| Play | Return or Space |
| Menu | M or Tab |
| Back | Backspace or Escape |
| Power | P |

| Option | Effect |
|---|---|
| `--scale N` | Window scale (default 3) |
| `--frames N` | Quit after N frames |
| `--headless` | No window |
| `--screenshot FILE.bmp` | Save the last frame on exit |
| `--card DIR` | Use DIR as the card root (read only) |
| `--system-dir DIR` | Read and write `/.gnarlyr/` (library, covers, resume, settings, favorites) in DIR |
| `--battery N` | Report N % battery (default 80) |
| `--charging` | Report the battery as charging |
| `--keys LIST` | Play a key script, for example `down,down,play,back:long` (`up`, `down`, `play`, `menu`, `back`, `power`, each optionally `:long`) |

Headless runs use a virtual clock for deterministic scripted screenshots. When running `echo_sim` directly, pass `--system-dir` to save state.

## Layout

| Path | Contents |
|---|---|
| `identity.json` | Name, maker, identifier, version, data folder, update file and playlist directive |
| `src/` | Portable app, freestanding C11, layered: `core` → `gfx`/`image`/`codec` → `fonts`/`ui`/`library`/`playback`/`settings` → `app`. `hal/` holds hardware interfaces only |
| `platform/sim` | SDL2 simulator: implements `hal`, owns `main()` |
| `platform/device/board` | Device-only drivers, start-up, linker scripts |
| `platform/device/logic` | Driver logic without hardware access, unit-tested on the host |
| `platform/device/core1` | Decoder programs for the second core |
| `tools/` | Python: image builder, size report, emulator, library indexer, `echo_sync.py`, font and fixture generators |
| `tests/` | One test executable per module, emulator scenarios, fixtures |
| `docs/` | The documents listed above |

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE). The bundled Inter font is under the SIL Open Font License 1.1 ([`assets/fonts/inter/OFL.txt`](assets/fonts/inter/OFL.txt)), and so are the glyph packs rendered from it. The simulator's decoders (dr_flac, dr_mp3, dr_wav) are public domain or MIT-0 ([`third_party/dr_libs/LICENSE`](third_party/dr_libs/LICENSE)). The player's MP3 decoder is libmad, GPL-2.0-or-later ([`third_party/libmad/COPYING`](third_party/libmad/COPYING)). The player's AAC decoder is FAAD2, GPL-2.0-or-later ([`third_party/faad2/COPYING`](third_party/faad2/COPYING)).
