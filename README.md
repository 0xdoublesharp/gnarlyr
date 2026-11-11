# Gnarlyr

Firmware for the full-size SNOWSKY (FiiO) ECHO music player, and the tools used to study FiiO's stock firmware for it.

The static Gnarlyr.com firmware site lives in [`site/`](site/README.md). Open `site/index.html` to preview it; it needs no build step or dependencies.

Gnarlyr builds on [Podium](https://podium-echo.online) and, through it, on [EchoPod](https://github.com/BasilSkyWalk/echopod). Both are GPL-3.0-or-later; credits and provenance are in [`firmware/CREDITS.md`](firmware/CREDITS.md) and [`firmware/UPSTREAM.md`](firmware/UPSTREAM.md). Snowsky, FiiO and Echo are names of their owners; this project is not made, endorsed or supported by FiiO.

## Help translate Gnarlyr

Some language pack drafts are generated with AI and may contain mistakes or awkward wording. If you speak another language, please help translate the interface or submit corrections as an issue or pull request. Include the English text and the suggested translation so we can find the right string. The source catalog and pack format are described in [the language documentation](firmware/docs/architecture.md#languages).

## Layout

| Path | Contents |
|---|---|
| [`firmware/`](firmware/README.md) | The firmware source, simulator, emulator, tests and documentation |
| `tools/firmware.py` | Build and test wrapper for the firmware |
| `tools/echo_debug.py`, `tools/echo_debug_mcp.py` | Host side of the developer builds' debug loop, as a command line that prints JSON and as an MCP server: `status`, `report` and `profile --elf ELF` read the player's kept debug reports from its internal drive over USB storage; `install IMAGE [--restart]` copies an update; `run SCRIPT` sends a debug script and returns its results; `wait` and `screenshots --out DIR` complete the loop. See [the architecture record](firmware/docs/architecture.md), Debug loop |
| `tools/echo_image.py`, `run_analysis.py`, `check_analysis.py`, `rea_session.mjs`, `ghidra/` | Analysis of FiiO's stock image; see [the stock firmware reference](docs/stock-firmware.md) |
| `tools/recovery/` | CMake project for the patched maskrom tool; see [`firmware/docs/device-recovery.md`](firmware/docs/device-recovery.md) |
| `tests/` | Tests for the stock-image parser and exporter and for `echo_debug.py` |

## Building the firmware

```sh
python3 tools/firmware.py setup
python3 tools/firmware.py build --preset device
python3 tools/firmware.py build --preset release
python3 tools/firmware.py test
python3 tools/firmware.py sim --card /path/to/music
```

The wrapper checks its prerequisites, keeps all build output on the Scratch volume (`/Volumes/Scratch/SnowskyEcho/build/`, one directory per git worktree) and never touches a player. Focused tests build only their executable dependencies. Temporary files and simulator state are also separate for each worktree.

While editing:

```sh
python3 tools/firmware.py test --regex '^test_player$'
python3 tools/firmware.py test --group smoke
python3 tools/firmware.py test --group host --list
python3 tools/firmware.py build --target echo_sim
```

`--group host` selects native unit tests. `--group smoke` checks playback, settings, queues, the updater, ARM boot and module fallback. `--list` shows the selected tests, fixtures and build targets without compiling. An unmatched selection fails. Run `test --jobs 6` once after integration for the complete suite.

`--jobs N` sets build and test parallelism; `--build-jobs N` and `--test-jobs N` override them separately. See [development workflow](docs/development.md) for selection and dependency details.

Prerequisites:

- CMake, SDL2, FFmpeg and clang-format on `PATH`.
- Python 3.14. Its Unicode tables reproduce the checked-in sort tables; 3.13 and 3.15 do not. The wrapper expects a virtual environment at `/Volumes/Scratch/SnowskyEcho/tools/podium-python314` with `firmware/tools/requirements.txt` installed; set `ECHO_VENV` to use another.
- Arm GNU Toolchain 15.3.rel2 (GCC 15.3.1) for `arm-none-eabi`, expected under `/Volumes/Scratch/SnowskyEcho/tools/arm-15.3-expanded/Payload/bin`; set `ECHO_ARM_BIN` to use another. The macOS arm64 package is at [Arm's package service](https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/15.3.rel2/arm-gnu-toolchain-15.3.rel2-darwin-arm64-arm-none-eabi.pkg), SHA-256 `779972fa545f738bfbba2ff0ae44d5ad7510b00cfb414702974844ba625412f3`. Check the digest, then unpack it with `pkgutil --expand-full`; no installer needs to run.

## Stock firmware analysis

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
PYTHONDONTWRITEBYTECODE=1 python3 tools/run_analysis.py
```

`run_analysis.py --help` lists its inputs; it refuses to reuse an existing run directory. Stock images, derived ELF files, Ghidra projects and reports stay on the Scratch volume and are never committed. A derived ELF is for analysis and must never be flashed.
