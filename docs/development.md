# Firmware development workflow

Start firmware work from the current `gnarlyr` branch. Each worktree gets its own build directory, temporary directory and simulator state under `/Volumes/Scratch/SnowskyEcho`. The wrapper stops if Scratch is not mounted and keeps the shared compiler cache separate.

## Edit and check one component

```sh
python3 tools/firmware.py test --regex '^test_player$'
python3 tools/firmware.py test --regex '^(test_settings|test_configured_controls)$'
```

The wrapper configures CMake, asks CTest for the selection and its fixtures, and uses CMake's file API to find executable dependencies. It builds the selected native executables and helpers passed to Python checks. CMake builds their libraries and generated-file dependencies normally. Script-only checks need no host build.

On a cold build, CTest can omit an executable's command until the binary exists. A native test with the same name as its target can still be built directly. Other unresolved cold selections use the complete host build. An empty selection fails before compilation. Selected ARM checks retain the device-image build fixture, so they run against a freshly built image.

Inspect a selection before running it:

```sh
python3 tools/firmware.py test --regex '^created_cards_pass_fsck$' --list
python3 tools/firmware.py test --group smoke --list
```

`--list` configures the build and prints the tests, automatically included fixtures and host build targets; it does not compile or execute tests.

## Test groups

| Command | Coverage |
| --- | --- |
| `test --group host` | Native unit tests named `test_*` |
| `test --group smoke` | Player, settings, Up Next, session journal, updater integrity, ARM home-screen boot and damaged-module fallback |
| `test --regex EXPRESSION` | Matching CTest checks and their required fixtures |
| `test` | Complete host, tools, generated-data, resource, simulator and ARM suite |

Run focused checks while editing, the smoke group before combining changes, and the complete suite once after the final integration edits. Keep passing results for untouched code. The smoke group complements the full suite and physical-device checks.

```sh
python3 tools/firmware.py test --group smoke --build-jobs 4 --test-jobs 6
python3 tools/firmware.py test --jobs 6
```

`--jobs` defaults to four and controls both build and test parallelism. The two overrides let tests run with different concurrency from compilation. ARM image checks reserve two CTest slots each. The wrapper has no option to skip a required build.

## Build or run the simulator

```sh
python3 tools/firmware.py build --target echo_sim
python3 tools/firmware.py sim --card /path/to/music
python3 tools/firmware.py build --preset release --target echo_size_report
```

`--target` can be repeated and is accepted only by `build`. Without it, `build` builds the complete preset. `sim` builds only `echo_sim` and its dependencies. Simulator state lives in `/Volumes/Scratch/SnowskyEcho/data/<worktree>/sim-system`; existing state in older locations is preserved.

## Resource checks and hardware acceptance

Before adding device behavior, choose its resident or module location and budget. Keep the release code reserve required by `firmware/CLAUDE.md`. Define caller-owned state, imported functions, cancellation and module-eviction behavior before connecting a new screen or codec.

Build a concrete candidate and complete the relevant software checks before a device window. Batch related player checks through the existing debug loop, record the running build, and follow the device-access rules in `AGENTS.md`. Build and test commands in this document do not access the player.
