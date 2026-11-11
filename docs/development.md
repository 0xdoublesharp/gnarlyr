# Firmware development workflow

Start each feature branch from the latest integration head agreed with the integration owner. Each worktree gets its own build directory, temporary directory and simulator state under `/Volumes/Scratch/SnowskyEcho`. The wrapper stops if Scratch is not mounted and keeps the shared compiler cache separate.

## Feature to player loop

1. Branch from the latest integration head and agree on files before editing. Keep Controls as the last Settings row.
2. Regenerate source and assets only through `python3 tools/firmware.py regen --jobs 2`. This uses the build environment, including Homebrew LLVM's `clang-format` and Python 3.14. It runs the string headers, browser language catalogue, fonts, glyph packs, Unicode folds, resampler and replay-gain table generators. Fixture builders retain their separate fixture-specific commands.
3. Run focused checks with `python3 tools/firmware.py test --jobs 2 --regex EXPRESSION`. Check generated freshness with `python3 tools/firmware.py regen --check --jobs 2`; this reads the committed outputs without rewriting them and fails on any stale generator.
4. Run `python3 tools/firmware.py sizes --jobs 2` and paste its table into the feature report. New Settings rows and modules must report their module bytes. The table builds both image presets, reports resident code, static RAM, overlays, all modules, core-1 banks and state, workspace and language slots, and fails on a slot overflow or a resident reserve below 4 KiB for release or 1 KiB for device. `sizes --existing --jobs 2` reads existing builds; rebuild after source changes before using that result for a candidate.
5. Hand the committed feature and focused results to the integration batch. The integration owner runs one complete suite on the combined batch with `python3 tools/firmware.py test --jobs 6` (up to `--jobs 10` when the machine is otherwise idle), then integrates the passing batch into `main`. Only one full suite may run on the machine at a time. Feature workers never run the full suite.
6. From clean `main`, prepare a device window with `python3 tools/firmware.py install-device --dry-run --jobs 2`. After owner approval for that exact install and review of the full-size Echo recovery route, run `python3 tools/firmware.py install-device --owner-approved --jobs 2`.

`install-device` reads status and the newest report through the existing `echo_debug.Tool` operations. It resolves the installed clean build tag to Git, refuses an already installed candidate, rebuilds the device image at clean HEAD, checks its embedded build tag, and freezes the image and every ELF/map, with checksums, under `/Volumes/Scratch/SnowskyEcho/reports/candidate-<full-sha>/`. A frozen image is immutable; a different image for the same commit is refused. Preserve the frozen installed candidate across development sessions.

If the installed candidate is missing, the command creates a disposable detached checkout of that exact commit under Scratch, builds with two jobs, freezes it, and removes the checkout. This historical build checkout is the sole exception to keeping active worktrees on backed-up storage; it contains no work in progress. Historical commits must have the wrapper's device-image target and the local toolchain prerequisites. Missing history, incompatible historical tooling or a proof failure stops the install for the integration owner to resolve.

The exact-image proof runs the installed updater with its own resident/USB/module ELFs, offers the unmodified new image, verifies slot 2 completes before slot 1, headers are invalidated first and committed last, every firmware write is immediately read back, both slots match, and writes stay inside the two slots and user volume. It boots the resulting slots using the new candidate's ELFs. Only the historical proof may omit the version-specific `hal_system_set_usb_answer` hook; ordinary emulator scenarios require it. The proof is also available as `firmware/.venv/bin/python firmware/tools/prove_upgrade.py --old CANDIDATE --new CANDIDATE`.

Before installing, the command copies persistent state from the connected player's internal data directory into `.local/device-backups/user-state/<UTC>/`, verifying each source and copy with SHA-256. It excludes `library.db`, `covers.bin`, `build.tmp`, `view.tmp`, `debug/` and AppleDouble `._*` files and refuses symlinks. The backup is private, persistent recovery data, not disposable build output. A dry run performs this backup and the proof, then stops before copying anything to the player or ejecting it.

The live step uses `echo_debug.Tool.install(image, restart=True)`, which checks the copied image, ejects and waits for reconnect. A new report must name the candidate build, contain zero fault registers and zero output underruns, and be complete. `install.json` and `summary.txt` in the candidate directory record the outcome, including failure after an attempted write; the command prints a one-line summary. It never appends to `notes/`. These checks establish only the named software and reconnect behavior; follow `AGENTS.md` for the owner's physical acceptance checks.

## Concurrent feature, coverage and fuzz work

Firmware integration uses `main` in `~/Workspaces/SnowskyEcho`. Feature, coverage and fuzz workers use separate branches and worktrees, with an agreed set of source files. The fuzz worker owns `firmware/tests/fuzz/` and its build, wrapper and documentation registrations. Each worker edits and builds in its own worktree. Preserve unfinished work with a source checkpoint before retiring its worktree.

Agree on the component and source files before starting a batch. The feature owner reserves files under active development; the coverage worker coordinates any overlapping bug fix first. Test registrations, including `firmware/tests/CMakeLists.txt`, travel in branch commits through the same integration process.

Hand off each completed component, aiming for a small batch every 20–30 minutes of active work. Include the exact commit and base, changed files, focused and smoke results, measured coverage changes, and a reproducer for each bug fix. Commit complete batches separately from unfinished work and continue the next independent component while integration is pending.

Coordinate each update to `main` so independent merges preserve the tested checkpoints and pending work. The integration owner serializes candidate builds and device installations. At a feature checkpoint, commit the completed feature changes, inspect the incoming diff and overlap, then merge the exact tested coverage commit. If unfinished delivery edits overlap, finish or park that component before merging; the coverage worker leaves those edits alone. Run the affected checks and smoke group on the combined tree, and report the resulting commit to the coverage worker. Resolve conflicts explicitly and rerun the checks they affect. Preserve a coverage worker's passing results when the merge leaves their inputs unchanged.

After integration, the coverage and fuzz workers merge the reported `main` commit into their clean branches before the next dependent batch. The fuzz worker also syncs before a new campaign when either the parser or persistence code under test changed. Keep Scratch build outputs, corpora and reports separate per worktree. Run the complete required suite once after the final integration edits, and repeat only for changes or failures that invalidate it. Source and software checks do not authorize player writes or establish hardware acceptance.

## Fuzz and invariant checks

The host fuzzers use Clang libFuzzer with AddressSanitizer and UndefinedBehaviorSanitizer. The wrapper keeps generated builds, evolving corpora and crash artifacts under `/Volumes/Scratch/SnowskyEcho`; only harness source and deterministic regression tests belong in Git.

```sh
python3 tools/firmware.py fuzz --target track_tags --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target playlist_reader --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target session_journal --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target library_database --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target mp4_demux --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target store --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target settings_codec --runs 10000 --seed 1
python3 tools/firmware.py fuzz --target cue_sheet --runs 100000 --seed 1
python3 tools/firmware.py fuzz
```

Without `--target`, the wrapper builds and runs every target for 10,000 executions with seed 1. Repeat `--target` to select several targets. A crash must be reproducible from its Scratch artifact and converted into an ordinary focused unit test before fixing production code. Hand off the regression, fix and passing focused fuzz campaign as one small commit; do not commit the corpus or artifact.

## Edit and check one component

```sh
python3 tools/firmware.py test --jobs 2 --regex '^test_player$'
python3 tools/firmware.py test --jobs 2 --regex '^(test_settings|test_configured_controls)$'
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
python3 tools/firmware.py test --group smoke --jobs 2
python3 tools/firmware.py test --jobs 6
```

`--jobs` defaults to four and controls both build and test parallelism. The two overrides let tests run with different concurrency from compilation. ARM image checks reserve two CTest slots each. The wrapper has no option to skip a required build.

## Build or run the simulator

```sh
python3 tools/firmware.py build --target echo_sim
python3 tools/firmware.py sim --card /path/to/music
python3 tools/firmware.py build --preset release --target echo_size_report
```

`--target` can be repeated and is accepted by `build` and `fuzz`. Without it, `build` builds the complete preset and `fuzz` runs every fuzzer. `sim` builds only `echo_sim` and its dependencies. Simulator state lives in `/Volumes/Scratch/SnowskyEcho/data/<worktree>/sim-system`; existing state in older locations is preserved.

## USB companion settings

The [companion control operations](companion-usb.md) expose validated settings discovery and changes through both CLI and MCP, with exact installed-build binding and saved-state verification after reconnect.

## Language-pack tools

The [browser builder and USB CLI/MCP workflow](language-packs.md) add and select external packs without changing device code. Managed installation validates the entire pack against the installed build from Git history, checks staged writes and refreshes the player through the existing USB debug loop.

## Resource checks and hardware acceptance

Before adding device behavior, choose its resident or module location and budget. Keep the release code reserve required by `firmware/CLAUDE.md`. Define caller-owned state, imported functions, cancellation and module-eviction behavior before connecting a new screen or codec.

Build a concrete candidate and complete the relevant software checks before a device window. Batch related player checks through the existing debug loop, record the running build, and follow the device-access rules in `AGENTS.md`. Build and test commands in this document do not access the player.
