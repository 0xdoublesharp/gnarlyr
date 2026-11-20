# Robustness checks

Two opt-in commands go beyond the normal test suite: long fuzz campaigns and static analysis. Neither runs in `firmware.py test` or in the CI that runs on pushes and pull requests. Both write only to Scratch.

## Long fuzzing

The CI smoke run, `python3 tools/firmware.py fuzz --runs 1000 --seed 1 --jobs 4`, is unchanged. It runs each fuzzer for a fixed number of inputs on a per-worktree corpus that it rebuilds every time.

A long campaign runs each fuzzer for a set time on a corpus that persists between runs:

```sh
python3 tools/firmware.py fuzz --long                        # every fuzzer, 10 minutes each
python3 tools/firmware.py fuzz --long --minutes 30 --target cue_sheet
python3 tools/firmware.py fuzz --long --hours 2 --jobs 6
```

`--minutes` and `--hours` set the time for each fuzzer, not for the whole campaign. `--jobs` sets how many fuzzers run at once. Each fuzzer uses about one processor, and the run takes about `fuzzers × time ÷ jobs`. `--seed` fixes libFuzzer's seed; without it, every run explores differently.

For each fuzzer the campaign:

1. Locks the corpus at `/Volumes/Scratch/SnowskyEcho/data/fuzz-corpus/<fuzzer>/`, so two campaigns cannot share it.
2. Adds seeds named by a hash of their content, so seeding again adds nothing new. The seeds are the smoke seeds, the repository fixtures, the generated tracks in `data/batch4-checks`, and small synthetic files in `data/fuzz-seeds/synthetic-v<N>/`. The synthetic files cover every codec and container ffmpeg can write, edge-case tags, embedded JPEG and PNG art, M4B chapters, CUE sheets, playlists, lyrics and theme packs built with `tools/theme_pack.py`. Each seed gets the framing its harness expects: the two-byte prefix for `playlist_reader`, a trailing control byte for `ogg_track`, control bytes plus raw packets taken from Ogg pages for `opus_decode`, and only raw packs for `theme_pack`. Seeds come only from these locations. No music library or memory card is read.
3. Measures coverage with a `-runs=0` pass.
4. Runs libFuzzer in fork mode with `-ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1`, so one crash does not end the run, and with the fuzzer's dictionary when it has one.
5. Merges and minimises the corpus with `-merge=1`. The old corpus is replaced only when the merge succeeds.
6. Measures coverage again, and writes artifacts and the summary.

Dictionaries live in `tests/fuzz/dictionaries/` and are combined per fuzzer at run time (`DICTIONARIES` in `tools/fuzz_campaign.py`). They hold ID3, Vorbis comment, APE and ASF tag keys, FLAC, Ogg, MP4, RIFF/AIFF, ASF, APE and DSD structure values, JPEG markers and PNG chunks, CUE keywords, playlist and LRC syntax, the theme-pack header, the `DS` file markers and FAT/exFAT boot values. The playlist start directive comes from `identity.json`. Fuzzers driven by control scripts (screens, navigation, the queue) take no dictionary.

Leak checking: LeakSanitizer is off by default on macOS, so the campaign sets `ASAN_OPTIONS=detect_leaks=1`. `tests/fuzz/lsan-suppressions.txt` suppresses only libFuzzer's own RSS-monitor thread, which LeakSanitizer otherwise reports at every exit. The firmware has no heap, so a leak report points at a harness.

### Results

Each run writes to `/Volumes/Scratch/SnowskyEcho/reports/fuzz/<date>/`:

- `summary-<time>.md` and `.json`: for each fuzzer the time, executions, executions per second, edge and feature coverage before and after, corpus files before and after the merge, and counts of crashes, timeouts, out-of-memory errors and leaks. The summary is rewritten as each fuzzer finishes, so a stopped run still reports.
- `<fuzzer>/fuzz.log`: the complete libFuzzer output.
- `<fuzzer>/crash-*`, `timeout-*`, `oom-*`, `leak-*`: inputs that triggered a failure. Each has a `.repro` file holding a single command line that replays it.

The command exits with an error when any fuzzer left an artifact or failed to run.

Reading the numbers: coverage that no longer grows means the fuzzer is saturated at its current harness. Merging normally makes the corpus smaller. A timeout found under heavy machine load may not reproduce. Replay it on its own (the `.repro` line) before treating it as a hang. For a crash, use the stack in `fuzz.log` and replay to separate a product fault from a harness fault (a `fuzz_require` that checks the wrong thing). Minimise an input with `<binary> -minimize_crash=1 -runs=10000 <artifact>`.

### Nightly job

`.github/workflows/fuzz-long.yml` runs nightly and on manual dispatch, never on pushes or pull requests. A manual run takes the minutes per fuzzer and an optional single target. The corpus is kept between runs in the Actions cache, and the reports are uploaded as an artifact.

## Static analysis

```sh
python3 tools/firmware.py analyze                     # all three parts
python3 tools/firmware.py analyze --part tidy --part analyzer --jobs 6
python3 tools/firmware.py analyze --part stack
```

Results go to `/Volumes/Scratch/SnowskyEcho/reports/analyze/<date>/`.

### clang-tidy

`--part tidy` builds the `dev` preset and runs clang-tidy on every source in its `compile_commands.json` under `src/`, `platform/sim/` and `platform/device/logic/`, skipping `third_party/`, `tests/` and the generated `src/fonts/`. The check set is `tests/analysis/clang-tidy.yaml`: `bugprone-*`, `clang-analyzer-*`, a few CERT C rules, `misc-no-recursion` (the project bans recursion), and readability checks that find mistakes rather than style. It leaves out checks that only restate project style or the compiler's own `-Wconversion`. Output: `clang-tidy.md`, `clang-tidy.json` and the raw `clang-tidy.log`. A unit that does not compile under clang-tidy is listed as not analysed. It is never counted as clean.

### Clang Static Analyzer

`--part analyzer` runs `clang --analyze` on the same sources and the same compile commands, with the sanitizer flags removed. It adds checkers beyond clang-tidy's defaults: `security.ArrayBound`, `core.BitwiseShift`, enum range casts, C string bounds and overlap, pointer subtraction and size, and division by zero after a test. Output: `analyzer.md`, `analyzer.json`, one SARIF file per unit under `sarif/`, and `analyzer.log`.

Host builds are LP64 and the player is ILP32. A width or overflow finding has to be judged against 32-bit `size_t` and pointers before it is ranked.

Device-only code is outside the `dev` compile database: `platform/device/board`, the core 1 programs and the overlays. Only the stack bound covers it.

### Suppression policy

Only new findings count. `tests/analysis/baseline.tsv` lists findings that were triaged and accepted, one per line: a fingerprint, the tool, the check, the path and the reason. The fingerprint is a hash of the tool, check, path, message (with numbers removed) and the text of the source line. Moving code does not change it, but editing the flagged line does. A finding whose fingerprint is in the baseline is reported as known. A baseline entry that no longer matches anything is reported as stale and should be removed. Every entry needs a reason, and the reader rejects an entry without one.

To add an entry, copy the fingerprint from the report and write why the finding is a false positive or acceptable. Fix real bugs instead of listing them. Do not use `NOLINT` comments: the project bans comments in code, and the baseline keeps the reasons in one place.

### Stack bound

`--part stack` builds the `device` preset into `build/analyze-stack` with `-fstack-usage -fcallgraph-info=su,da` added through `CFLAGS` and `LDFLAGS`, with the compiler cache off so every object writes its `.su` and `.ci` files. The link step adds the flags too. The core 0 program links with link-time optimisation, so GCC writes `echo_device.elf.ltrans0.ltrans.ci` for the code that actually ends up in the image. Objects outside link-time optimisation add their own per-file `.ci`. Functions that the linker removed are dropped by checking the ELF symbol table.

For each program (core 0 and every core 1 decoder), the tool builds the call graph and computes the deepest path from each entry point:

- core 0: `device_reset` (start-up and the main loop), `board_systick_handler` (SysTick, the only enabled interrupt; the vector table has no external interrupts and NVIC lines are disabled), and `device_fault_handler` (the fault exceptions, which end in a panic loop);
- core 1: `core1_reset` and `core1_tick`.

The combined core 0 figure is main path + 32-byte exception frame + SysTick path, plus another frame and the fault path. All exceptions share the same priority except the faults, so SysTick cannot nest within itself. The naked entry stubs only branch, so the handlers are taken as the entries. Each core 1 program is compared with its own stack reserve, not the 16 KB core 0 stack.

Reported per program: the deepest paths with their frame sizes, recursion cycles, functions with no frame size (assembly, libgcc, anything not compiled with the flags), functions with dynamic frames, and indirect call sites.

Limits:

- Indirect calls cannot be followed. The tool finds address-taken functions by scanning the loaded ELF contents for words that hold a Thumb function address. Each indirect call site is charged the deepest such function once, without following further indirect calls. That figure is an estimate, not a proven bound. Charging every indirect call with every address-taken function, transitively, makes screen and module dispatch look recursive, and that bound would be infinite.
- Code in overlays and demand-loaded modules lives in separate ELF files and is reached only through indirect calls, so the core 0 figure does not include it.
- Functions without frame sizes count as zero.
- Inline assembly that moves the stack pointer is not seen.
- If the static figure for core 0 is below the stack peak measured in the emulator, edges are missing, and the figure must not be read as a bound.
