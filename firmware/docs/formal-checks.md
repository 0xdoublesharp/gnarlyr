# Formal checks

Two kinds of formal check sit beside the tests:

- **CBMC harnesses** run the C code that reads files from the card and internal storage on every possible file content up to a stated size. They check memory safety and a few format properties.
- **TLA+ models** describe the update, trial and rollback path, and the core 0 / core 1 audio hand-off, as state machines. TLC explores every interleaving within stated bounds.

The checks run only when someone asks for them. They are not part of `python3 tools/firmware.py test`, the `host` or `smoke` groups, the CTest registration, or the push and pull-request CI. The only CI entry is the manually started `Formal checks` workflow (`.github/workflows/formal.yml`).

The first runs were made against `main` at `bb0cb274` and found four defects in the update path and the core hand-off. The fixes are commits `153090e6` (promotion), `8fb67ea1` (both-slot order), `3ea575c3` (rolled-back record) and `751de675` (hand-off generation and release fence). The models now describe the fixed code. Each fixed defect keeps a `_before` configuration that sets that one fix's constant to `FALSE` and still finds the old violation. The results and line numbers below come from runs on the tree with the fixes.

## Running the checks

```sh
python3 tools/firmware.py formal                        # every default harness and model
python3 tools/firmware.py formal --cbmc                 # CBMC harnesses only
python3 tools/firmware.py formal --tla                  # TLA+ models only
python3 tools/firmware.py formal --harness core_handoff # one harness or model
python3 tools/firmware.py formal --harness 'id3_*,flac_*' --jobs 6  # shell patterns, several at once
python3 tools/firmware.py formal --list                 # names, without running anything
python3 tools/firmware.py formal --all                  # also the opt-in harnesses (hours)
python3 tools/firmware.py formal --cover --harness NAME # blocks a CBMC harness reaches
python3 tools/firmware.py formal --closure              # contracts and reader functions without a proof
python3 tools/firmware.py formal --repro                # replay counterexamples on the real code (host cc)
```

`--harness` takes an exact name or a shell pattern, repeated or separated by commas; a pattern also selects opt-in harnesses. `--jobs N` runs N checks at once. Each checker is single-threaded, so give it the broker's `$COMPUTE_JOBS`. CPU time is measured per child process, so it stays correct with `--jobs`. `FORMAL_REPORTS=<folder>` replaces the report folder, for runs on another host.

`python3 tools/formal.py` takes the same options. The entry point uses only Python's standard library and needs neither the ARM toolchain nor a firmware build. It stops if `/Volumes/Scratch` is not mounted.

Each run writes to `/Volumes/Scratch/SnowskyEcho/reports/formal/<worktree>/<date-time>/`:

- `summary.md` and `summary.json`: one row per check. A `ledger-<host>.jsonl` beside the run folders keeps one line per check with its CPU time, for budget accounting.
- `cbmc/<harness>/`: the goto programs and a log with the exact commands and CBMC's full output, including counterexample traces.
- `tla/<model>/`: copies of the spec and config, TLC's state files and a log with the full TLC output, including any trace.

TLC runs on those copies, so it never writes into the repository. `TMPDIR` and `java.io.tmpdir` point at `/Volumes/Scratch/SnowskyEcho/tmp/formal/`.

Each check ends in one of these results:

| Result | Meaning |
|---|---|
| `passed` | No property failed, and none was expected to |
| `expected-failure`, `expected-violation` | The check was written to fail (the canary, and models that test a property the design is known not to keep, or an assumption). Its trace is in the log |
| `failed` | A property failed that was meant to hold |
| `unexpected-pass` | A check written to fail passed: either the code changed or the check went vacuous |
| `error` | The tool did not produce a verdict, or CBMC reached a function with no body |
| `timeout` | The check ran out of time |

The exit code is 0 when every check ended as intended, 1 otherwise, and 2 when a tool is missing.

### Installing the tools

`formal.py` checks for the tools before it runs anything. If one is missing it prints these steps and exits with code 2.

| Tool | Version checked | Install |
|---|---|---|
| CBMC (`goto-cc`, `goto-instrument`, `cbmc`) | 6.11.0 | macOS: `brew install cbmc`. Ubuntu 24.04: `ubuntu-24.04-cbmc-6.11.0-Linux.deb` from the CBMC release on GitHub, SHA-256 `b3721aa541038384d7801ea3aeabbcddc3e8845ac8f1cbff637cf8dec7481ac8`, then `sudo apt-get install ./<deb>` |
| Java | 11 or later; checked with OpenJDK 21 | `brew install openjdk@21` or `sudo apt-get install openjdk-21-jre-headless` |
| `tla2tools.jar` | TLA+ tools 1.7.4 (TLC 2.19, 8 August 2024), the latest non-pre-release | `python3 tools/formal.py --fetch-tla` |

`--fetch-tla` downloads `https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar` to `/Volumes/Scratch/SnowskyEcho/tools/tla/tla2tools.jar`. It keeps the file only if it matches both pinned checksums:

- SHA-1 `bee4a54f3ee3d4afc347c3240ec2d9e93b075104`, as published in the v1.7.4 release notes.
- SHA-256 `936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88`, computed from the downloaded file that matched that SHA-1.

Before each TLC run, `formal.py` checks the jar against the SHA-256.

The CI workflow installs the same CBMC release, checks its SHA-256, installs OpenJDK 21 and runs `--fetch-tla`. It uploads the report folder as an artifact. You can choose `all`, `cbmc` or `tla`, and optionally one harness. It also installs `gcc-multilib` and compiles with `-ffreestanding`, so that `-m32` builds on a stock amd64 runner. The workflow has not yet run on GitHub; only the local runs described here have been made.

`tests/test_formal.py` covers the entry point with a stubbed tool runner, so `python3 -m unittest discover -s tests` stays fast and runs no checker.

## Layout

| Path | Contents |
|---|---|
| `firmware/formal/checks.json` | The manifest: CBMC flags, each harness's sources, stubs, bounds and unwind limit, and each TLA+ model's spec, config and expected result |
| `firmware/formal/cbmc/harnesses/` | One harness file per reader family; the entry point is always `harness()` |
| `firmware/formal/cbmc/stubs/` | `card.c` (the nondeterministic file), `hash.c` (checksum seam), `hal.c` (file, version and language-memory HAL), `theme.c` (signature seam), `i18n.c` (the pack parser's contract, for the loader harness), `builtins.h` (maps `__builtin_memcpy` to CBMC's modelled `memcpy`), `formal.h` (shared declarations and the target checks) |
| `firmware/formal/cbmc/catalogue/core/strings.h` | A four-string, three-segment catalogue that replaces the generated one for `language_pack` only |
| `firmware/formal/cbmc/units/` | One unit harness per parsing function (see Decomposed harnesses); the entry point is `harness()` |
| `firmware/formal/cbmc/contracts/` | Function and loop contracts for the unit harnesses, as `FORMAL_CONTRACT_<function>` and `FORMAL_LOOP_<function>_<n>` macros |
| `firmware/formal/repro/` | Host programs that replay a CBMC counterexample on the unmodified product code |
| `firmware/formal/tla/` | `UpdateRollback.tla`, `CoreHandoff.tla` and one `.cfg` per checked configuration |
| `tools/formal.py`, `tests/test_formal.py` | The entry point and its unit test |

The harness C follows the firmware rules: no comments, formatted with `.clang-format`. No firmware name, identifier or folder appears in any of these files. The C reads them from `FIRMWARE_*` definitions taken from `identity.json`.

## CBMC harnesses

### Common set-up

- **Target.** Every harness is compiled with `goto-cc -m32 -funsigned-char -ffreestanding`: 32-bit `int`, `long` and pointers, little-endian, unsigned `char`, as on the Cortex-M3. `stubs/formal.h` checks all three with `_Static_assert`, so a harness that compiled for another target would not build. CBMC labels the goto program's library as i386, but the type widths and byte order are the ones that matter, and they match.
- **Checks on every harness:** `--bounds-check --pointer-check --pointer-overflow-check --signed-overflow-check --div-by-zero-check --undefined-shift-check`. CBMC 6 also turns a call to a function with no body into a failing property, and `formal.py` reports such a call as an `error`.
- **Unwinding.** Most harnesses are *complete* within their bound. They run with `--unwinding-assertions`, so a loop that could run past the unwind limit, for any file up to the bound, fails the check. That makes a pass a proof for every file up to the bound.

  The tag, CUE, playlist and theme harnesses did not finish that way within about 10 CPU-minutes, even at 16 to 48 bytes. `tags_vorbis_comment` and `playlist_reader` are *depth-bounded* instead, with `--no-unwinding-assertions`. CBMC 6 adds unwinding assertions by default, so the option must be given. A pass then covers every path in which no loop runs more than *N* − 1 times, for unwind *N*. Paths with longer loops are not examined. The others are opt-in; see below.
- **Unsigned overflow.** `--unsigned-overflow-check` is off globally because firmware code wraps on purpose: store generations, the language row check `size - 1 - 2n`, and the hash. It is on for `library_database` and the opt-in `theme_pack_v1`, where every offset and size sum must stay below the file size and a wrap would be a bug.
- **Uninitialised memory.** CBMC itself gives uninitialised memory an arbitrary value, so every property is checked for every content such memory could have. It does not report the read itself. `goto-instrument --uninitialized-check` would, but it tracks only direct assignments to locals. Every reader here fills its buffers through a callee (`hal_file_open(&file)`, `read_source(..., header, ...)`), so that check reports each of them falsely. It runs on the canary only, to show it works.
- **The file.** `stubs/card.c` stands for the file on the card. The file's size is nondeterministic up to `FORMAL_CARD_BYTES`. A read:
  - may fail at any time, in which case it overwrites the caller's buffer with arbitrary bytes, as a part-done read would;
  - fails if it reaches past the size, as `fat_read` does;
  - otherwise copies the bytes;
  - always asserts that the caller's buffer can hold the requested length.

  The bytes come from one nondeterministic array, so two reads of the same offset agree. Harnesses marked *fresh* set `FORMAL_CARD_FRESH`. Each successful read then returns new arbitrary bytes instead, which keeps the solver fast. That over-approximates the file, so a pass is still a proof for every real file. A failure found in fresh mode must be replayed with consistent reads before it counts.
- **Checksum seams.** `hash_update` (FNV-1a) returns an arbitrary value but asserts that the whole span it would hash is readable. `hash_store` and `hash_load` keep their byte layout. The theme pack's `theme_pack_crc` is given a nondeterministic body with `goto-instrument --generate-function-body`. `theme_signature_inspect` returns an arbitrary verdict. As a result, every checksum comparison both passes and fails, so the code behind each checksum is reached for every content. The checksum code itself is not checked.
- **Reachability.** `python3 tools/formal.py --cover [--harness NAME]` runs `cbmc --cover location` on a harness. For each product source it reports how many basic blocks some file up to the bound can reach. A file's total also counts functions the harness does not drive.

### Results

The figures are CPU seconds of the checker (`formal.py` measures its child processes). They come from a run on 2026-11-22 at `f3861a63`, on mbp2019 (Intel, about 0.45 times the M2's speed per core) through the shared broker, except `settings_codec`, which was re-run on the development Mac (Apple Silicon) after its source list was corrected. The opt-in results and the reachability figures below come from the first runs at `bb0cb274`.

| Harness | Entry points | Bound | Unwind | Mode | Properties | Result | CPU |
|---|---|---|---|---|---|---|---|
| `canary` | planted faults in harness code | 16 B | 18 | complete, with `--uninitialized-check` | 57 | fails on exactly its 2 planted properties, as intended | 1 s |
| `library_database` | `library_open`, `library_read_*`, `library_album_key` | 160 B, fresh | 40 | complete, unsigned overflow on | 969 | passed | 573 s |
| `scan_record` | `music_record_read`, `music_check_*` | 528 B | 26 | complete | 733 | passed | 3 s |
| `tags_vorbis_comment` | `tags_read_vorbis_comment` | 64 B, fresh | 4 | depth-bounded | 4,634 | passed | 450 s |
| `playlist_reader` | `playlist_reader_open`, `_next` | 64 B, fresh | 4 | depth-bounded | 374 | passed | 816 s |
| `settings_codec` | `settings_decode`, `_encode`, `settings_buttons_decode`, `settings_file_newer` | 96 B | 98 | complete | 2,929 | passed | 19 s |
| `playback_store` | `store_pair_open`, `store_load`, `store_scan_next` | 1,100 B, fresh | 514 | complete | 1,030 | passed | 138 s |
| `user_state_records` | `user_state_list_decode`, `_mark_request`, `_age`, `update_trial_decode` | 44 B | 24 | complete | 1,376 | passed | 3 s |
| `language_pack` | `i18n_attach`, `i18n_pack_segment`, `i18n_attach_segment`, `i18n_text` | 128 B pack, 32 B module | 132 | complete | 871 | passed | 25 s |
| `language_loader` | `language_load`, `language_use_module` | 256 B, fresh; 192 B memory | 20 | complete | 538 | passed | 251 s |

Five harnesses are opt-in (`"default": false`). `formal.py` skips them unless they are named with `--harness` or the run uses `--all`. None has a useful verdict as a whole-reader check. The complete runs and the depth-bounded runs at unwind 12 stopped at a 30-minute wall limit after 10 to 18 CPU-minutes. The decomposed harnesses below give most of those readers verdicts, some of them conditional; these five stay for comparison.

| Harness | Status |
|---|---|
| `tags_id3v2` | no verdict at 32 to 64 B. Unwind 3 finishes in 7 s but cannot complete the 3-byte magic compare |
| `tags_flac` | no verdict. A run at unwind 4 "passes" in 1 s but reaches 21 of 354 blocks in `tags_vorbis.c`, because CBMC needs *k* + 1 unwindings to finish a *k*-iteration loop. The manifest asks for unwind 6 |
| `tags_mp4` | no verdict; unwind 4 is vacuous for the same reason. The manifest asks for unwind 6 |
| `cue_sheet` | no verdict at 16 to 64 B |
| `theme_pack_v1` | no verdict past the header check. A depth-bounded run at unwind 12 "passes" (496 s CPU), but `text_valid` and `zeroes` need at least 21 iterations on a 32-byte field, so `theme_pack_open` cannot accept a pack in that run. Only header rejection paths are covered. The manifest asks for unwind 130, which has not been run |

Reachability, from `--cover` runs (blocks reached in each driven source, out of all blocks in the file):

| Harness | Blocks reached (of all blocks in the file) | CPU |
|---|---|---|
| `scan_record` | `music_check.c` 71/84, `music_record.c` 24/69 (`music_record_write` and the comparisons are not driven) | 1 s |
| `settings_codec` | `settings.c` 350/466 | 1,045 s |
| `playback_store` | `store.c` 156/349 (the write path is not driven) | 45 s |
| `user_state_records` | `user_state_list.c` 48/69, `user_state.c` 72/370 (backup, restore and the continuity pass are not driven), `update_trial.c` 22/54 | 1 s |
| `language_pack` | `i18n.c` 125/159 | 4 s |
| `language_loader` | `language.c` 108/112, `language_module.c` 66/66 | 542 s |
| `tags_vorbis_comment` | `tags_vorbis.c` 50/354, `track_tags.c` 17/310, `replay_gain.c` 17/294. At depth 4 the key table and key comparisons cannot complete, so this pass covers the length and count handling, not field matching | 361 s |
| `library_database`, `playlist_reader` | not measured; a `--cover` run costs more than the check itself | |

### What each harness proves

For every file content up to the bound, every listed entry point stays inside its buffers and the file, with no signed overflow, undefined shift or division by zero. In complete mode, every loop also ends within the unwind limit. In depth-bounded mode, those properties hold on every path in which no loop runs more than *N* − 1 times, for unwind *N*.

- **`canary`.** A planted overflow and an uninitialised read in harness code. It must fail on exactly `count_marks.array_bounds.1` and `count_marks.uninitialized_local.1`. Any other result means the set-up is broken.
- **`library_database` (`library.db`, `DSDB` version 2).** `library_open` (header, section table, the content fingerprint walk) on a file of up to 160 bytes (fresh). On any accepted file it then calls, at any index: `library_read_track`, `_album`, `_group` (any section), `_folder`, `_index` (any section), `_string` (capacity 0 to 16) and `library_album_key`. It asserts that every accepted section lies inside the file, music counts never exceed section counts, and strings end inside their buffer. The header alone is 128 bytes, so this leaves up to 32 bytes of section data. The playlist and hidden-track overlays (`Library.extra`, `Library.hidden`) are not set, because no file sets them.
- **`scan_record` (`scan.bin`, `DSS2`).** `music_record_read`, `music_check_firmware_is_new`, `music_check_covers_pending`, `music_check_interrupted_scans`, `music_check_generation_moved` and `music_record_settled`, on a file of up to 528 bytes with an arbitrary firmware version string. 528 bytes reaches the scan guard at offset 512. The task named one reader, "library/scan.bin, format 2, `DSS2`". The code has two: the library database (`DSDB` version 2) and the music check record (`DSS2`). Each has its own harness.
- **`tags_id3v2`, `tags_flac`, `tags_mp4` (opt-in, no verdict yet).** `track_tags_read` for:
  - MP3: ID3v2, then the MPEG frame scan and ID3v1, at 64 bytes;
  - FLAC: metadata blocks, Vorbis comment and picture, at 96 bytes;
  - MP4: atom walk, `ilst`, at 96 bytes.

  Every tag text should end inside its 256-byte field, and a raw picture should lie inside the file. None of the three has finished at a depth that reaches past the magic check (see the opt-in table). Ogg, ASF, APE, WAV, AIFF and DSD are compiled in but not driven.
- **`tags_vorbis_comment` (depth-bounded).** `tags_read_vorbis_comment` directly, at any offset and limit inside a 64-byte file, for both Vorbis and Opus framing. It covers the comment parser, the key table, the replay-gain fields and the base64 picture probe without the FLAC wrapper.
- **`cue_sheet` (opt-in, no verdict yet).** One `cue_sheet_next` on a 64-byte file. It starts either from the opened file or from an arbitrary reader position with arbitrary pending and building flags, numbers and times. The assertions are that titles, performers and the album fields end inside their fields. It has not finished at any depth tried. The resumed state keeps the album and entry texts empty, which under-approximates a sheet that already holds text.
- **`playlist_reader` (depth-bounded).** One `playlist_reader_next` on a 64-byte file, with an entry buffer of 1 to 12 bytes. It starts either from the opened file or from any reader state: any position, any cached chunk, either encoding, any start time. That state over-approximates every later call. Every entry ends inside its buffer.

  At unwind 4, `playlist_reader_open`'s UTF-8 scan (`is_utf8`) finishes only for files of at most three sequences, or with an invalid byte among the first three, which sends them down the Windows-1252 path. Longer valid UTF-8 files are cut off before the reader runs. Each line seen holds at most three characters. The pass covers that much and no more.
- **`settings_codec` (`settings.bin`, `buttons.bin`).** `settings_decode`, `settings_encode` of the result, `settings_buttons_decode` and `settings_file_newer`, on 0 to 96 bytes of arbitrary content. A decoded language code ends inside its field, and the preset is in range.
- **`playback_store` (the two-bank `StoreFile` format).** `store_pair_open` with an arbitrary bank size, `store_load` at any offset and `store_scan_next` at any position, on a 1,100-byte file (fresh). An inline payload never exceeds its block. This covers `order.bin`, `session.bin`, `mixes.bin`, `moments.bin`, `track_keys.bin`, `listens.bin`, `recap.bin`, `theme.bin` and the other store files at the block level. Each format's payload decoder is not covered.
- **`user_state_records`.** On arbitrary bytes: `user_state_list_decode` (`backup/list.bin`), `user_state_mark_request` (`continuity.bin`), `user_state_age` for every catalogue entry, and `update_trial_decode` (`trial.bin`). Build names end inside their fields, the newest slot is a slot or none, a restore request names a slot or none, and a trial state is known.
- **`language_pack` (the pack parser).** `i18n_attach` on an arbitrary pack of up to 128 bytes in memory, `i18n_text` for any string of the resident segment, then `i18n_pack_segment` and `i18n_attach_segment` for any module segment whose block fits 32 bytes, as the loader guarantees, and `i18n_text` for any string of that segment. Every looked-up text starts and ends inside its block.

  The generated catalogue (910 strings in 7 segments) is replaced by a catalogue of 4 strings in 3 segments, with the same header and row layout. Without that, the smallest valid pack would be over 2 KB. The parser's logic does not depend on the catalogue's size, but the original counts were not checked.
- **`language_loader` (`language.c`, `language_module.c`).** `language_load` with an arbitrary code, then `language_use_module` for any module, reading from a file or a built-in copy (fresh, up to 256 bytes) into 192 bytes of language memory. The parser is replaced by `stubs/i18n.c`, which can return any verdict and any index values allowed by its contract. Every read fits its buffer for every such verdict.

  The contract matters. The loader rounds `info.resident` up to 4 bytes before comparing it with the memory's capacity (`language.c:48`), and that sum wraps for a `resident` within 3 of 2³². The real parser keeps `resident` far smaller (`row_matches`, `i18n.c:44`). The stub encodes that bound, and without it the loader check fails.
- **`theme_pack_v1` (`.gtheme`, `DSTP` version 1, opt-in, no verdict yet).** `theme_pack_open` on up to 640 bytes (fresh): the 512-byte header, the asset rows, the CRC walk and the signature trailer. The assertions are that every accepted asset lies inside the file and that the name and version end inside their fields. The palette decode runs, with its FNV stubbed. This harness predates the current theme format: it calls `theme_pack_open` without the check argument and reads asset offsets that `ThemePack` no longer keeps, so it does not build on the current code.

### Decomposed harnesses

The opt-in whole-reader harnesses above give no verdict for the ID3v2, FLAC, MP4, CUE and theme readers, and only shallow ones for the Vorbis comment and playlist readers. The unit harnesses in `firmware/formal/cbmc/units/` replace them with one harness per parsing function. All of them are opt-in (`"unit": true`, `"default": false`): `--all` or a `--harness` pattern runs them, and nothing in `test`, CTest or CI does.

**How a unit harness works.**

- **Contracts live beside the harnesses.** Each function's contract is a macro, `FORMAL_CONTRACT_<function>` (requires, assigns, ensures), in `contracts/<family>.h`. Each loop's contract is `FORMAL_LOOP_<function>_<n>` (assigns, invariant, decreases), where `<n>` counts the function's `for` and `while` heads in source order.
- **The product source is never edited.** `formal.py` copies each source the harness lists under `patch` into the run folder. It inserts each macro name directly after the matching function signature or loop head, on the same line, so CBMC's line numbers match the repository. The run stops on any of these:
  - a contract names a function defined more than once;
  - a function has some of its loops annotated but not all;
  - a planted change does not match exactly once.

  The headers reach every translation unit with `-include`. A harness reaches `static` functions by including the copied `.c` file.
- **Assume-guarantee (DFCC).** `goto-instrument --dfcc harness --enforce-contract F --replace-call-with-contract G ... --apply-loop-contracts` checks F against its contract and replaces each listed callee G with its contract. Each G has its own harness that enforces G's contract. DFCC also checks every write against the assigns clause. A contract variant, `F/V`, applies when the caller passes pointers that share an object, as `cue_sheet_next` does: its reader and line buffer live in one `CueSheet`.
- **Loops.**
  - A loop with a contract is proved for any number of iterations, so the verdict holds for every file size.
  - Before DFCC, every other loop is unrolled with `--unwinding-assertions` to the harness's `unwind` bound. That bound is the format's real maximum: 256-byte tag texts, 1,024-byte frames and lines, 32 theme assets, 8 Vorbis keys, the 12-entry ID3 frame table, 64-byte replay-gain texts.
  - CBMC's own `--unwind 34` then covers only DFCC's library loops.
- **Pruning before instrumentation.** Function pointers are resolved to direct calls, the bodies of replaced callees are removed, and unreachable functions are dropped. This keeps DFCC from instrumenting the picture-streaming code that the tag readers never call.
- **Closure.** `formal.py --closure` and `tests/test_formal.py` check two things:
  - every contract a harness replaces is the target of some other harness (for `tag_unsync_read`, a plain harness written to prove it by hand). Whether that harness passed is a run result, rolled up in the reader table.
  - every function in the reader files (`families` in `checks.json`) is the target of a harness or is listed as `outside` the scope.

  After a run, a passing harness whose replaced contracts were not proven in the same run says so in its detail.
- **Reachability.** `--cover --harness NAME` runs `cbmc --cover location` on the same instrumented program. It reports the reached blocks of the harness's `targets` only: the function under test and the helpers it inlines, not whole files.

  The goal counts depend on the unrolling: a loop unrolled to N copies contributes N copies of its blocks. Where a loop's real bound is smaller than the harness bound, the unreachable copies lower the percentage. The harnesses therefore set exact per-loop bounds (`unwindset`) where a loop has a known short maximum. The unreached goals were listed by source line for every harness below 90 %. They are of two kinds:
  - unrolled copies past a loop's real maximum, for example `next_utf8` line 178, `put_code_point` 158, `r128_value` 269, `divided` 56–61 and `has_bom` 98;
  - DFCC's loop-contract and wrapper blocks at loop heads and function entries, for example `text_valid` 40 and 50, `tag_decode_gain_text` 314 and the first line of each enforced function.

  A pass counts as a verdict only when its coverage was measured and is high. Three harnesses were found vacuous this way and fixed, as described under Findings.
- **Planted faults.** Each family has a `<harness>_planted` variant whose `plant` entry makes a one-line off-by-one in a bounds check, on the Scratch copy only. The run must fail on a memory or contract property at the planted line or contract, or the harness counts as broken. The expected line is given as an offset from the line holding the planted text, so `formal.py` finds it again after edits elsewhere in the file. The plant is a manifest rewrite applied by `formal.py`, not a preprocessor define, so the product source never carries it. The results table shows which plants are caught.

**Assumptions every unit harness makes.**

- The file is `stubs/card.c` in fresh mode, so every successful read returns new arbitrary bytes. That over-approximates any real file. The card `ByteSource` has a NULL context.
- No buffer passed to a parser is larger than `FORMAL_OBJECT_LIMIT` (512 KiB). The player's static RAM is 272 KiB, so no real buffer is larger. CBMC runs with `--object-bits 11`.
- `hash_update`, `theme_pack_crc` (where replaced) and `theme_signature_inspect` keep the seams described under Common set-up.
- `tag_unsync_read` takes a `void *` context. CBMC 6.11 loses the value of a function pointer read through that context reinterpreted as `TagUnsync *`: a trace showed the view's source as `formal_card_read` while the comparison with it failed. Its contract therefore does not state the view's source.
  - The plain harness `tag_unsync_read` is written to prove the function for a view over the card stub. It restricts the indirect call to `formal_card_read`, and CBMC asserts that restriction. It also checks by hand that no byte outside the buffer changes and that the view keeps its source. It has no verdict yet (see Without a verdict), so every ID3 result that reads through the view is conditional on this contract.
  - That this view is the only one is shown by the code, not checked: `read_through` copies the reader's source, which every ID3 contract requires to be the card.
- Playlist and CUE: `FORMAL_PLAYLIST_NO_WRAP` is a precondition of `read_line`, `playlist_reader_next` and the CUE harnesses. In UTF-8 mode it limits the file to 2³² − 4 bytes, so a UTF-8 file within 3 bytes of 4 GiB is outside every playlist and CUE verdict, not just outside the termination proof. Covering it needs `is_utf8`'s guarantee that the last sequence is complete, and the contracts do not carry that. Windows-1252 mode has no such limit.
- CUE: a `CueSheet`'s text fields start with byte 255 zero. The scan state is in zero-initialised memory, and the `handle_line` contract states that no writer puts a non-zero byte there. That contract has no verdict yet.
- Theme: `crc_valid`'s contract requires at least the 512-byte header. `theme_pack_open` checks the size against the header before any later call, but since the asset checks were rewritten no harness checks the call site (see Without a verdict).

**Results.** The second table rolls the unit results up to the readers named in the task. A reader-level pass is conditional on every contract without a verdict that its harnesses replace, directly or through another harness. `tag_unsync_read` counts only for the ID3 harnesses, because the FLAC, Vorbis and MP4 contracts require the card source. Each row of the first table is the latest run of that harness after its last contract change, measured with CBMC 6.11.0. Every library and theme row was re-run on 2026-11-22 at `f3861a63`, after the ID3 skip fix (`3f2e0823`), the move of the byte-order helpers to `core/bytes.h` (`7c0d6765`) and the theme format changes; the planted variants were re-run on the M2 after their expected lines were made relative to the planted text. The playlist and CUE rows, `rg_number` and `tag_unsync_read` keep their earlier runs: their sources did not change, and the last two have no verdict. Coverage was re-measured for `id3_skip`, `flac_blocks`, `flac_picture`, `id3_frames` and `mp4_item`, whose targets changed; the other coverage figures come from the earlier runs. `bytes_be16` and `bytes_be32` are counted inside their callers, since the instrumented program keeps no blocks of their own. Runs were made through the shared broker on the M2 (Apple Silicon) or on mbp2019 (Intel, about 0.45 times the M2's speed per core), as the CPU column shows, so CPU seconds from the two hosts are not directly comparable. "Coverage" is the share of the target functions' basic blocks that `--cover location` reaches in the same instrumented program. "Caught" means a planted fault failed the named property, as intended.

| Harness | Checks | Replaced callees | Method and bound | Coverage | Result | CPU, host |
|---|---|---|---|---|---|---|
| `tag_read` | `tag_read` | `tag_unsync_read` | no other loops | 40/42 (95 %) | passed: 0 of 155 properties failed; **conditional** on unproven `tag_unsync_read` | 4 s, mbp2019 |
| `tag_decode` | `tag_decode` | none | loop contracts on `tag_decode`; other loops unrolled to 4 | 203/239 (84 %) | passed: 0 of 821 properties failed | 9 s, mbp2019 |
| `tag_decode_gain_text` | `tag_decode_gain_text` | none | loop contracts on `empty_gain_tail`, `tag_decode_gain_text`; no other loops | 90/105 (85 %) | passed: 0 of 290 properties failed | 7 s, mbp2019 |
| `replay_gain_field` | `replay_gain_field` | none | other loops unrolled to 23 | 26/30 (86 %) | passed: 0 of 17282 properties failed | 7 s, mbp2019 |
| `replay_gain_offer` | `replay_gain_offer` | `number_of`, `gain_value`, `peak_value` | no other loops | 28/30 (93 %) | passed: 0 of 353 properties failed; **conditional** on unproven `number_of` | 4 s, mbp2019 |
| `rg_number` | `number_of` | none | other loops unrolled to 65 | 2764/2880 (95 %) | timeout: stopped at the time limit | 3277 s, M2 |
| `rg_gain_value` | `gain_value` | none | other loops unrolled to 201 (exact: fraction_value.0:21) | 74/81 (91 %) | passed: 0 of 1584 properties failed | 142 s, mbp2019 |
| `rg_peak_value` | `peak_value` | none | other loops unrolled to 201 | not measured (cover run stopped after 3282 s) | passed: 0 of 12293 properties failed | 1,513 s, mbp2019 |
| `replay_gain_offer_r128` | `replay_gain_offer_r128` | none | other loops unrolled to 7 | 78/93 (83 %) | passed: 0 of 243 properties failed | 3 s, mbp2019 |
| `tag_unsync_read` | `tag_unsync_read` | none | loop contracts on `tag_unsync_read`; other loops unrolled to 64; plain harness, frame asserted by hand | not measured | error: cbmc exited 15 without a verdict | 2706 s, mbp2019 |
| `id3_unsync` | `remove_unsync` | none | loop contracts on `remove_unsync`; no other loops | 27/29 (93 %) | passed: 0 of 170 properties failed | 4 s, mbp2019 |
| `id3_value_end` | `value_end` | none | loop contracts on `value_end`; no other loops | 28/30 (93 %) | passed: 0 of 94 properties failed | 4 s, mbp2019 |
| `id3_genre_text` | `offer_genre_text` | none | other loops unrolled to 256 | not measured (cover run stopped after 990 s) | passed: 0 of 31521 properties failed | 476 s, mbp2019 |
| `id3_text_frame` | `read_text_frame` | `tag_read`, `tag_decode`, `remove_unsync`, `value_end`, `offer_genre_text` | loop contracts on `for_each_value`; other loops unrolled to 256 | 2757/2813 (98 %) | passed: 0 of 45929 properties failed; **conditional** on unproven `tag_unsync_read` | 792 s, mbp2019 |
| `id3_skip_value` | `skip_value` | `tag_read` | loop contracts on `skip_value`; no other loops | 36/38 (94 %) | passed: 0 of 173 properties failed; **conditional** on unproven `tag_unsync_read` | 33 s, mbp2019 |
| `id3_picture_frame` | `read_picture_frame` | `tag_read`, `skip_value` | no other loops | 117/119 (98 %) | passed: 0 of 809 properties failed; **conditional** on unproven `tag_unsync_read` | 192 s, mbp2019 |
| `id3_gain_frame` | `read_replay_gain_frame` | `tag_read`, `remove_unsync`, `value_end`, `tag_decode_gain_text`, `replay_gain_field`, `replay_gain_offer` | other loops unrolled to 65 | 187/194 (96 %) | passed: 0 of 1748 properties failed; **conditional** on unproven `number_of`, `tag_unsync_read` | 47 s, mbp2019 |
| `id3_frame` | `read_frame` | `read_text_frame`, `read_picture_frame`, `read_replay_gain_frame` | other loops unrolled to 12 (exact: tag_same_text.0:6) | 151/162 (93 %) | passed: 0 of 2504 properties failed; **conditional** on unproven `number_of`, `tag_unsync_read` | 12 s, mbp2019 |
| `id3_frames` | `read_frames` | `read_frame`, `tag_read` | loop contracts on `read_frames`; other loops unrolled to 4 | 95/97 (97 %) | passed: 0 of 2344 properties failed; **conditional** on unproven `number_of`, `tag_unsync_read` | 180 s, mbp2019 |
| `id3_header` | `tags_read_id3v2` | `read_frames`, `tag_read` | other loops unrolled to 256 (exact: tag_same_text.0:5) | 46/48 (95 %) | passed: 0 of 12513 properties failed; **conditional** on unproven `number_of`, `tag_unsync_read` | 28 s, mbp2019 |
| `id3_v1` | `tags_read_id3v1` | `tag_read`, `tag_decode` | other loops unrolled to 30 | 128/132 (96 %) | passed: 0 of 3590 properties failed; **conditional** on unproven `tag_unsync_read` | 26 s, mbp2019 |
| `tag_set_duration` | `tag_set_duration` | none | other loops unrolled to 65 | 360/407 (88 %) | passed: 0 of 134 properties failed | 4 s, mbp2019 |
| `vorbis_entry` | `offer_entry` | `tag_read`, `tag_decode`, `replay_gain_field`, `replay_gain_offer`, `replay_gain_offer_r128` | other loops unrolled to 32 (exact: base64_picture_is_front.0:7,field_for_key.0:9,tag_same_text_folded.0:23,key_length.0:33) | 463/501 (92 %) | passed: 0 of 5490 properties failed; **conditional** on unproven `number_of` | 180 s, mbp2019 |
| `vorbis_comment_walk` | `tags_read_vorbis_comment` | `offer_entry`, `tag_read` | loop contracts on `tags_read_vorbis_comment`; no other loops | 37/40 (92 %) | passed: 0 of 2263 properties failed; **conditional** on unproven `number_of` | 103 s, mbp2019 |
| `id3_skip` | `tags_skip_id3v2` | `tag_read` | loop contracts on `tags_skip_id3v2`; other loops unrolled to 2 (exact: tag_same_text.0:4) | 28/30 (93 %) | passed: 0 of 397 properties failed | 5 s, mbp2019 |
| `flac_picture` | `read_flac_picture` | `tag_read` | other loops unrolled to 2 | 27/29 (93 %) | passed: 0 of 536 properties failed | 7 s, mbp2019 |
| `flac_blocks` | `tags_read_flac` | `tags_skip_id3v2`, `tag_read`, `tags_read_vorbis_comment`, `read_flac_picture`, `tag_set_duration` | loop contracts on `tags_read_flac`; other loops unrolled to 4 | 34/36 (94 %) | passed: 0 of 2292 properties failed; **conditional** on unproven `number_of` | 24 s, mbp2019 |
| `mp4_read_atom` | `tags_mp4_read_atom` | `tag_read` | other loops unrolled to 4 | 32/34 (94 %) | passed: 0 of 470 properties failed | 5 s, mbp2019 |
| `mp4_find_child` | `tags_mp4_find_child` | `tags_mp4_read_atom` | loop contracts on `tags_mp4_find_child`; other loops unrolled to 5 | 33/35 (94 %) | passed: 0 of 843 properties failed | 7 s, mbp2019 |
| `mp4_sound_track` | `sound_track` | `tags_mp4_read_atom`, `tags_mp4_find_child`, `tag_read` | loop contracts on `sound_track`; other loops unrolled to 5 | 57/59 (96 %) | passed: 0 of 1165 properties failed | 14 s, mbp2019 |
| `mp4_duration` | `read_duration` | `tags_mp4_find_child`, `tag_read`, `tag_set_duration` | no other loops | 29/31 (93 %) | passed: 0 of 838 properties failed | 9 s, mbp2019 |
| `mp4_gain_item` | `read_gain_item` | `tags_mp4_find_child`, `tags_mp4_read_atom`, `tag_read`, `replay_gain_field`, `replay_gain_offer` | loop contracts on `read_gain_item`; other loops unrolled to 29 | 76/78 (97 %) | passed: 0 of 2278 properties failed; **conditional** on unproven `number_of` | 18 s, mbp2019 |
| `mp4_item` | `read_item` | `tags_mp4_read_atom`, `read_gain_item`, `tag_read`, `tag_decode` | loop contracts on `read_item`; other loops unrolled to 30 (exact: item_for.0:9,offer_number.0:9,offer_number.1:9) | 326/420 (78 %) | passed: 0 of 4616 properties failed; **conditional** on unproven `number_of` | 99 s, mbp2019 |
| `mp4_items` | `read_items` | `tags_mp4_read_atom`, `read_item` | loop contracts on `read_items`; other loops unrolled to 5 | 29/31 (93 %) | passed: 0 of 2078 properties failed; **conditional** on unproven `number_of` | 18 s, mbp2019 |
| `mp4_tags` | `read_tags` | `tags_mp4_find_child`, `read_items` | no other loops | 29/31 (93 %) | passed: 0 of 1101 properties failed; **conditional** on unproven `number_of` | 11 s, mbp2019 |
| `mp4_top` | `tags_read_mp4` | `tags_mp4_find_child`, `sound_track`, `read_duration`, `read_tags` | no other loops | 27/29 (93 %) | passed: 0 of 1744 properties failed; **conditional** on unproven `number_of` | 12 s, mbp2019 |
| `playlist_byte_at` | `byte_at` | none | no other loops | 27/29 (93 %) | passed: 0 of 163 properties failed | 6 s, mbp2019 |
| `playlist_valid_sequence` | `valid_sequence` | `byte_at` | other loops unrolled to 3 | 35/37 (94 %) | passed: 0 of 135 properties failed | 6 s, mbp2019 |
| `playlist_is_utf8` | `is_utf8` | `byte_at`, `valid_sequence` | loop contracts on `is_utf8`; no other loops | 33/35 (94 %) | passed: 0 of 279 properties failed | 5 s, mbp2019 |
| `playlist_open` | `playlist_reader_open` | `is_utf8`, `byte_at` | other loops unrolled to 3 | 50/59 (84 %) | passed: 0 of 197 properties failed | 4 s, mbp2019 |
| `playlist_code_point` | `next_code_point` | `byte_at` | other loops unrolled to 3 | 37/39 (94 %) | passed: 0 of 226 properties failed | 6 s, mbp2019 |
| `playlist_put` | `put` | none | other loops unrolled to 4 | 32/34 (94 %) | passed: 0 of 387 properties failed | 327 s, mbp2019 |
| `playlist_read_line` | `read_line` | `next_code_point`, `put`, `byte_at` | loop contracts on `read_line`; no other loops | 50/52 (96 %) | passed: 0 of 909 properties failed | 51 s, mbp2019 |
| `playlist_next` | `playlist_reader_next` | `read_line` | loop contracts on `playlist_reader_next`; other loops unrolled to 25 (exact: note_directive.0:16,note_directive.1:12) | 138/151 (91 %) | passed: 0 of 1127 properties failed | 20 s, mbp2019 |
| `playlist_cue_view` | `playlist_reader_next/formal_cue_reader_next` | `next_code_point`, `put`, `byte_at` | loop contracts on `playlist_reader_next`, `read_line`; other loops unrolled to 25 (exact: note_directive.0:16,note_directive.1:12) | not measured | timeout: stopped at the time limit | 983 s, mbp2019 |
| `cue_handle_line` | `handle_line` | none | other loops unrolled to 1024 (exact: after_word.0:10,find_word.0:7,read_index.0:4,copy_text.0:256) | not measured | timeout: stopped at the time limit | 2881 s, mbp2019 |
| `cue_next` | `cue_sheet_next` | `playlist_reader_next/formal_cue_reader_next`, `handle_line` | loop contracts on `cue_sheet_next`; no other loops | 61/63 (96 %) | passed: 0 of 1322 properties failed; **conditional** on unproven `handle_line`, `playlist_reader_next/formal_cue_reader_next` | 21 s, mbp2019 |
| `cue_usable` | `cue_sheet_usable` | `cue_sheet_next` | loop contracts on `cue_sheet_usable`; no other loops | 25/27 (92 %) | passed: 0 of 1017 properties failed; **conditional** on unproven `handle_line`, `playlist_reader_next/formal_cue_reader_next` | 15 s, mbp2019 |
| `cue_open` | `cue_sheet_open` | `playlist_reader_open` | no other loops | 25/27 (92 %) | passed: 0 of 166 properties failed | 4 s, mbp2019 |
| `theme_crc` | `theme_pack_crc` | none | loop contracts on `theme_pack_crc`; no other loops | 26/28 (92 %) | passed: 0 of 95 properties failed | 4 s, mbp2019 |
| `theme_zeroes` | `zeroes` | none | loop contracts on `zeroes`; no other loops | 27/29 (93 %) | passed: 0 of 128 properties failed | 4 s, mbp2019 |
| `theme_text` | `text_valid` | `zeroes` | loop contracts on `text_valid`; other loops unrolled to 4 | 96/114 (84 %) | passed: 0 of 400 properties failed | 70 s, mbp2019 |
| `theme_url` | `url_valid` | `text_valid` | other loops unrolled to 8 | 23/25 (92 %) | passed: 0 of 202 properties failed | 4 s, mbp2019 |
| `theme_version` | `version_valid` | none | other loops unrolled to 7 | 23/25 (92 %) | passed: 0 of 2307 properties failed | 5 s, mbp2019 |
| `theme_header` | `header_valid` | `text_valid`, `zeroes`, `url_valid`, `version_valid` | no other loops | 43/47 (91 %) | passed: 0 of 364 properties failed | 5 s, mbp2019 |
| `theme_crc_walk` | `crc_valid` | `theme_pack_crc` | loop contracts on `crc_valid`; no other loops | 22/24 (91 %) | passed: 0 of 111 properties failed | 9 s, mbp2019 |
| `theme_palette` | `decode_palette` | none | other loops unrolled to 40 (exact: theme_palette_decode.0:21) | 121/128 (94 %) | passed: 0 of 1961 properties failed | 6 s, mbp2019 |
| `tag_decode_planted` | `tag_decode` | none | planted fault | not measured | caught: caught put_code_point.assigns at line 150 | 3 s, M2 |
| `id3_unsync_planted` | `remove_unsync` | none | planted fault | not measured | caught: caught remove_unsync.pointer_dereference at line 94 | 1 s, M2 |
| `flac_picture_planted` | `read_flac_picture` | `tag_read` | planted fault | not measured | caught: caught read_flac_picture.postcondition at line 195 | 6 s, mbp2019 |
| `mp4_gain_item_planted` | `read_gain_item` | `tags_mp4_find_child`, `tags_mp4_read_atom`, `tag_read`, `replay_gain_field`, `replay_gain_offer` | planted fault | not measured | caught: caught gain_atom_text.pointer_dereference at line 208 | 6 s, M2 |
| `playlist_put_planted` | `put` | none | planted fault | not measured | caught: caught put.postcondition at line 163 | 361 s, mbp2019 |
| `cue_handle_line_planted` | `handle_line` | none | planted fault | not measured | timeout: stopped at the time limit | 3037 s, mbp2019 |
| `theme_text_planted` | `text_valid` | `zeroes` | planted fault | not measured | caught: caught text_valid.overflow at line 42 | 20 s, M2 |

| Reader | Top harnesses | Result | Unproven contracts the result rests on |
|---|---|---|---|
| ID3v2 (`tags_read_id3v2`, `tags_read_id3v1`) | `id3_header`, `id3_v1` | passed, passed | `number_of`, `tag_unsync_read` |
| FLAC (`tags_read_flac`) | `flac_blocks` | passed | `number_of` |
| Vorbis comment (`tags_read_vorbis_comment`) | `vorbis_comment_walk` | passed | `number_of` |
| MP4 (`tags_read_mp4`) | `mp4_top` | passed | `number_of` |
| CUE sheet (`cue_sheet_open`, `_next`, `_usable`) | `cue_open`, `cue_next`, `cue_usable` | passed, passed, passed | `handle_line`, `playlist_reader_next/formal_cue_reader_next` |
| Playlist (`playlist_reader_open`, `_next`) | `playlist_open`, `playlist_next` | passed, passed | none: unconditional within the stated assumptions |
| Theme pack (`theme_pack_open`) | none on the current format | no verdict: `theme_open` was withdrawn when the asset checks were rewritten | the header, text, URL, version, CRC and palette checks pass on their own |

**Without a verdict.**

- `tag_unsync_read` (the ID3 unsynchronisation view). Two formulations stopped without a verdict: a buffer of any size up to 512 KiB (2,475 CPU-seconds on mbp2019), then a fixed 128-byte buffer (2,706 s). The cost is the chunk loop: 64 unrolled writes through `*(*out)++` inside a loop proved by contract. Every ID3 harness that reads through `tag_read` relies on its contract. A seam that would help is a version of `unsync_take` that returns the byte count instead of advancing a pointer to a pointer, so that each write's bound is a plain index.
- `rg_number` (`number_of`, the replay-gain number scanner). Its cover run reaches 95 % of its blocks, but the check stopped at 3,600 s (3,277 CPU-seconds on the M2). The 64-character unrolling of `decimal_part`, with a 64-bit mantissa multiply in each step, does not converge. `replay_gain_offer`'s pass relies on its contract. A loop contract on `decimal_part`, bounding the mantissa below 10¹² and keeping `at` within `length`, is the next step.
- `playlist_cue_view` (`playlist_reader_next` checked against the CUE variant). It passed while its precondition was vacuous. Fixed, it stopped at 1,200 s (983 CPU-seconds on mbp2019): the two 1,024-entry existentials on the line buffer are expanded in full. `cue_next`'s pass relies on this contract.
- `cue_handle_line` (one CUE line: keyword match, quoted value, `INDEX` times, text copies). It passed while vacuous. Fixed, it and its planted variant stopped at 3,600 s (2,881 and 3,037 CPU-seconds on mbp2019), so CUE has no reader-level verdict and its planted fault is not caught. The line loops are unrolled to the real 1,024-byte line, and every scanning helper is inlined. Contracts on `skip_blanks`, `value_of` and `read_digits`, stated as offsets into `sheet->line`, are the next step.
- The theme pack's asset, style and payload checks. They were rewritten for the current theme format after these harnesses were written: `theme_pack_open` takes a check level, `assets_valid` takes a layout, and the per-asset checks became `descriptor_valid`, `slot_valid`, `icon_valid`, `codec_valid`, `key_taken`, `read_asset` and the style and payload walks. `theme_asset_entry`, `theme_assets` and `theme_open` no longer matched the code and were withdrawn with their contracts. Their earlier passes applied to the earlier format only. The functions without a harness are listed as `pending` for the `theme_v1` family in `checks.json`; `--closure` accepts them but prints each one, so a clean closure does not mean they are checked. The header, text, URL, version, CRC and palette harnesses still match the code and were re-run.
- Coverage was not measured for `id3_genre_text` (stopped at 990 s), `rg_peak_value` (stopped at 3,282 s), `tag_unsync_read`, `cue_handle_line` or `playlist_cue_view`. Their checks passed, except `tag_unsync_read`'s, but the passes do not show by themselves that the code was reached.
- Outside the scope of this work, and so without a unit harness:
  - the Ogg page walk in `tags_vorbis.c`, including the search back from the end of the file for the final page (`last_page`, `page_ending_file`);
  - the MPEG frame scan in `tags_mpeg.c` (its leading ID3 skip is the shared `tags_skip_id3v2`, which `id3_skip` checks);
  - `track_tags_read`'s dispatch;
  - the ASF, APE, RIFF and DSD readers.

  `families` in `checks.json` lists the Ogg functions and the picture-streaming helpers as `outside`.

**Compute.** The re-run on 2026-11-22 took 6,754 CPU-seconds (1.9 hours) on mbp2019 for the default harnesses, the library and theme units, the planted variants and five cover runs, and under two CPU-minutes on the M2. The decomposition work before it took 13.1 CPU-hours over 22 recorded runs:
- the M2: 4.5 hours of checks and 2.8 of cover runs;
- mbp2019: 5.6 hours of checks and 0.2 of cover runs.

Runs stopped by hand are not recorded, so the real total is somewhat higher. Most of the time went to the five harnesses without a verdict (`tag_unsync_read`, `rg_number`, `playlist_cue_view` and the two CUE line harnesses), the unsplit `replay_gain_offer` and two runs of `theme_assets` (2,380 and 2,254 CPU-seconds). Most other harnesses finish in under a minute. The largest that finish are `id3_text_frame` (858 s), `id3_genre_text` (631 s) and `playlist_put` (327 s), on mbp2019.

**Findings from the decomposed harnesses.**

- **The ID3 skip loop could wrap the offset and loop forever (FLAC and MP3).** Low severity, a hang on crafted input. Fixed by `3f2e0823`; the description below is of the code before the fix.
  - `skip_id3` (`tags_vorbis.c:190`, and the same loop in `tags_mpeg.c:48`) adds `10 + synchsafe(size)` to a 32-bit offset for each ID3 header it skips. Nothing stops that sum from passing 2³².
  - The harness of that time, `flac_skip_id3`, failed its `decreases` clause (`skip_id3.loop_decreases.1`, line 194) and recorded it as an expected failure. CBMC's trace uses a file of 4,159,700,984 bytes. At offset 4,093,640,694 it reads an `ID3` header with size bytes `7F 18 00 00`, a synchsafe 266,731,520. The next offset, 4,093,640,694 + 10 + 266,731,520, wraps to 65,404,928, below the one before. Any such counterexample needs a file of at least 2³² − 2²⁸ bytes.
  - `formal.py --repro` runs `repro/id3_skip_wrap.c` on the unmodified readers. A synthetic `ByteSource` of size `0xFFFFFFF0` holds 16 chained ID3v2.4 headers; each skips 2²⁸ − 1 bytes, and the last skip lands exactly on 2³², which wraps to offset 0. `track_tags_read` with `TAG_FORMAT_FLAC` and with `TAG_FORMAT_MP3` then reads offset 0 again and again until the program's read limit (4,096 reads) stops it. Without that limit the reader loop does not terminate on the host. What the player does was not observed: a watchdog restart or a stalled scan job are both possible.
  - The callers pass the FAT file size without a cap: the library scan (`library_scan.c:374`), the CUE scan (`library_scan_cue.c:181`) and the cover pass (`cover_build.c:394`).
  - A FAT32 file can be up to 2³² − 1 bytes, so the file is legal but has to be built for the purpose.
  - The fix replaced both loops with one helper, `tags_skip_id3v2` in `tags_id3.c`, which stops at the current header when the next skip would pass the file size. Its harness, `id3_skip`, proves that the loop ends for every file and that the returned offset is at most the file size. `flac_blocks` replaces the helper with that contract. The repro entry is marked `"expect": "fixed"`: `--repro` now reports `fixed` when neither format cycles, and `regressed`, a failure, if either cycles again.
- **No other memory-safety or contract defect was found.** That holds within the stated assumptions, and only as far as the reader table's conditions allow: ID3v2, FLAC, Vorbis comment and MP4 rest on contracts without a verdict, and CUE has no reader-level verdict. Every other counterexample during development came from a contract that was too weak, and each was traced and fixed in the contract:
  - 32-bit wrap in an `ensures` that the code cannot reach;
  - a negative `TagField`, which CBMC allows because the enum's type is signed;
  - a frame clause that let the unsync view's source be overwritten.
- **Three harnesses were vacuous until coverage showed it.**
  - `cue_handle_line` reached 10 of 18,623 target blocks, and `playlist_cue_view` 12 of 388. Their contracts required a parameter to equal a pointer into an object that DFCC allocates during the check (`line == sheet->line`), and a harness argument can never equal that, so the precondition was false.
  - `vorbis_entry`'s file-source branch (`read_file_bytes`, 0 of 2 blocks) failed the same way through `source.context == reader`.
  - All three now use `__CPROVER_pointer_in_range_dfcc`, which assigns the pointer when the precondition is assumed and checks it when asserted. The CUE planted fault "passed" for the same reason. Now it runs out of time like the unplanted harness.


### Not covered, and seams worth adding

- **Above the bound and the unwind limit.** A complete pass of a whole-reader harness says nothing about files larger than its bound. A depth-bounded pass also says nothing about paths whose loops run longer. The decomposed harnesses close most of this: their file-walking loops are proved by loop contracts for any file size. What they leave open is listed under Decomposed harnesses, Without a verdict.
- **Not driven.**
  - The Ogg page walk and final-page search, and the ASF, APE, WAV/AIFF and DSD tag readers.
  - The theme pack's asset table, styles and payloads (see Without a verdict).
  - The MP4 chapter and timed-metadata readers.
  - The cover image decoders (JPEG, PNG).
  - `fat` and `exfat`.
  - The headphone and parametric files, bookmarks and personal tags.
  - `covers.bin` and the library gain table.
  - The rescan job's own read of `scan.bin`.
- **Checksum code.** FNV, the theme CRC-32 and the signature check are replaced at the seams described above.
- **The language catalogue** at its real size.
- **Seams.** These would make deeper checks affordable, but the product code was not changed to fit the harnesses:
  - Expose `library_content`'s block size as a compile-time option.
  - The decomposed harnesses reach `static` functions by including the source file, so the per-frame handlers and `cue_sheet`'s `handle_line` needed no new entry points.
  - Let `unsync_take` (`tags_picture.c`) return a byte count instead of advancing `uint8_t **out`, so that `tag_unsync_read` can be proved.
  - Give `ByteSource` implementations a typed context, or a contract-friendly wrapper, so that a contract can state which source a view reads from without reinterpreting `void *`.
  - Add a pure decode function for the settings and store headers that takes a precomputed checksum, so the real FNV can stay in.
  - Add a `ByteSource`-based variant of `music_check_read_at`, so `scan.bin` needs no HAL stub.

## TLA+ models

TLC runs one worker for every model. The 1.7.4 release fixed an unsound liveness check with several workers (tlaplus issue 971); one worker avoids the question. No configuration uses symmetry.

Each model whose expected result is a violation names the property in `checks.json`. `formal.py` fails the check if TLC reports any other property, so a `TypeOK` slip cannot pass for an expected result. TLC stops at the first violation it finds. The configurations that expect one therefore check only that property, and a companion configuration checks the rest. The canary names its two expected CBMC properties the same way.

The wall times in these tables come from one sequential `formal.py` run on the development Mac at load averages of about 8 to 15.

### Update, trial and rollback: `UpdateRollback.tla`

**What is modelled.**

- **Firmware slots.** Two slots, each with a header owner (`hdr`, `NONE` when erased) and the image whose USB overlay sits at `USB_OVERLAY_IMAGE_OFFSET` (`ovl`).
- **Slot copy.** A copy has three steps: erase the first chunk, copy the body including the overlay chunk, and write the header chunk last. Each step is read back. This keeps what `slot_copy_run` makes visible: a slot is valid only after its header, and a part-copied slot can still hold the old overlay.
- **The trial record** `trial.bin`: state and attempts.
- **Files on internal storage:** the update file and `BOTHSLOT.REQ`.
- **The boot sequence:** the ROM's slot choice, counting, rollback, the continuity pass, the app, USB sessions, the updater, and promotion.
- **One user-state file** (`settings.bin`), with its continuity mark, the two backup slots and the `newer/` copy. The file's format version is chosen per build.

The images are the installed build `G` and one or two updates, `A` and `B`. Each update may be broken: it crashes or calls `board_trial_fail` before it settles. Each may also use format version 1 or 2. All four combinations are explored. The owner can:

- copy an update file (bounded by `MaxCopies`), or delete it (`OwnerMayDeleteUpdate`);
- place `BOTHSLOT.REQ` (`AllowBothRequest`);
- change settings, switch the player off, and connect USB with a session that does or does not confirm.

Power can be cut after every step, up to `MaxCuts` times. Optionally, a slot write may fail (`WritesFail`) and a single-sector write may tear (`SectorAtomic = FALSE`).

**Fix constants.** `FixPromotion` (promotion requires a valid slot 1, an invalid slot 1 ends the trial, and a copy refuses an erased source header), `FixBothOrder` (the both-slot install writes slot 1 first when only slot 2 is valid) and `FixRecordAfterPass` (the rolled-back record is deleted after the continuity pass). They are `TRUE` in every configuration except the `_before` ones.

**Assumptions** (constants, each also run with the other value):

- `RomFallsBack`: the ROM starts slot 2 when slot 1's header is invalid. `architecture.md` says this has not been measured.
- `SectorAtomic`: a one-sector write of the trial record or `settings.bin` is all-or-nothing on power loss.

**Abstracted away.**

- Chunk-by-chunk progress, apart from the overlay chunk.
- The slot table and modules.
- FiiO's stock image. It takes the same both-slot path as `BOTHSLOT.REQ`.
- `ALREADY_INSTALLED`, modelled as "the file's image is the valid one in slot 1".
- All user-state files but one.
- Settings restore requests from Settings → Backups and from `echo_debug.py`.
- Time: 20 s settling is an event.
- The trial diagnostics.

**Properties.**

| Property | Kind | Meaning |
|---|---|---|
| `AlwaysBootable` | invariant | slot 1 valid, or slot 2 valid and the ROM falls back |
| `NoOverwriteBeforeVerify` | invariant | while a slot's body or header is being written, the other slot is valid |
| `PreviousImageKept` | invariant (as worded) | some bootable slot holds a good image |
| `KnownGoodKept` | invariant (design intent) | `PreviousImageKept`, unless the owner asked for both slots |
| `SettingsAvailable` | invariant | `settings.bin`, its `newer/` copy, or a named backup holds a valid generation |
| `RollbackRestoresImage` | invariant | after a rollback start, until that start's first USB session, slot 1 holds slot 2's image |
| `RollbackRestoresSettings` | invariant (as worded) | after a rollback, `settings.bin` equals what the previous build had when the trial build first started |
| `PreviousSettingsRecoverable` | invariant (design intent) | after a rollback, that content is in `settings.bin` or in a named backup slot, so Settings → Backups can restore it |
| `RestoredBuildCanSave` | invariant | a build running with no trial (no record, or a rolled-back record) can write its settings (the file is not of a later format) |
| `RollbackPassDone` | invariant | the app never runs while a rolled-back record is still in place, so the continuity pass always ran before the record went |
| `TrialResolves` | liveness | a running trial eventually leaves *running*: it is promoted, rolled back, or replaced by another install |
| `TrialEnds` | liveness | a trial record that is *running* or *promoting* is eventually deleted |

The base spec has weak fairness on power-on, on every firmware step, on a broken build's crash and on settling. `SpecOwnerConfirms` adds strong fairness on a confirming USB session.

The three rollback properties (`RollbackRestoresImage`, `RollbackRestoresSettings`, `PreviousSettingsRecoverable`) apply from the rollback start until its first USB session: the code clears `rolled_back` at the first notice, and an update installed in a later session may rewrite slot 1. Before this scoping, a failed reinstall in such a session showed up as a `RollbackRestoresImage` violation with slot 2 still valid; the first runs had not reported it because TLC stopped at the shorter `AlwaysBootable` trace.

**Configurations and results.**

| Model | Config | Expect | Result | States generated (distinct) | Wall time |
|---|---|---|---|---|---|
| `update_rollback` | `UpdateRollback.cfg`: one update, 2 cuts, 2 copies, ROM falls back, atomic sectors, owner confirms, also `RestoredBuildCanSave` and `RollbackPassDone` | passed | passed | 56,969 (36,659), depth 114 | 18 s |
| `update_witness_rollback` | same constants, `RollbackWitness`: a rollback start is reachable | violated | violated as expected | 4,644 (3,243) | 6 s |
| `update_witness_promotion` | `PromotionWitness`: a promotion is reachable | violated | violated as expected | 2,707 (2,071) | 7 s |
| `update_witness_clear` | 1 cut, the owner may delete the update file, `ClearWitness`: the clear branch is reachable | violated | violated as expected | 677 (560) | 6 s |
| `update_promotes_partial_slot` | 1 cut, the owner may delete the update file | passed | passed | 28,176 (19,250), depth 102 | 9 s |
| `update_promotes_partial_slot_before` | same, `FixPromotion = FALSE`, `AlwaysBootable` | violated: `AlwaysBootable` | violated as expected | 1,052 (864), 17 steps | 7 s |
| `update_write_failure` | 1 cut, slot writes may fail, safety invariants | passed | passed | 99,463 (61,628), depth 129 | 8 s |
| `update_write_failure_before` | same, `FixPromotion = FALSE`, `AlwaysBootable` | violated: `AlwaysBootable` | violated as expected | 2,210 (1,583), 20 steps | 7 s |
| `update_write_failure_liveness` | same constants as `update_write_failure`, `TrialResolves` | violated: liveness | violated as expected | 43,310 (27,223) | 10 s |
| `update_both_request` | 1 cut, `BOTHSLOT.REQ` allowed | passed | passed | 69,655 (42,749), depth 110 | 8 s |
| `update_both_request_before` | same, `FixBothOrder = FALSE`, `AlwaysBootable` | violated: `AlwaysBootable` | violated as expected | 1,315 (1,010), 15 steps | 7 s |
| `update_both_request_known_good` | same as `update_both_request`, `KnownGoodKept` only | passed | passed | 69,655 (42,749) | 7 s |
| `update_both_request_literal` | same, `PreviousImageKept` | violated | violated as expected | 685 (456) | 6 s |
| `update_two_images` | updates `A` and `B`, 1 cut | violated: `KnownGoodKept` | violated as expected | 75,009 (52,737), 35 steps | 13 s |
| `update_two_images_other` | same, every other invariant and `TrialResolves` | passed | passed | 411,266 (290,118), depth 130 | 51 s |
| `update_trial_waits` | base spec, no owner fairness | violated: liveness | violated as expected | 21,034 (14,440); TLC stops where it finds the counterexample, so counts vary between runs | 8 s |
| `update_settings_literal` | 1 cut, `RollbackRestoresSettings` | violated | violated as expected | 7,436 (5,049) | 6 s |
| `update_lost_continuity_pass` | 2 cuts, `RestoredBuildCanSave`, `RollbackPassDone` | passed | passed | 56,969 (36,659), depth 114 | 7 s |
| `update_lost_continuity_pass_before` | same, `FixRecordAfterPass = FALSE`, `RestoredBuildCanSave` | violated | violated as expected | 16,096 (10,158), 38 steps | 6 s |
| `update_torn_sectors` | 1 cut, sector writes may tear | violated: `SettingsAvailable` | violated as expected | 29 (25), 5 steps | 6 s |
| `update_rom_slot1_only` | 1 cut, the ROM starts only slot 1 | violated: `AlwaysBootable` | violated as expected | 157 (105), 8 steps | 6 s |

**Model-to-code mapping** (`751de675`).

| Model action | Code |
|---|---|
| `PowerOn` | boot ROM slot choice (assumption, `architecture.md`, Firmware slots and the boot ROM); `board/main.c:368` calls `board_trial_boot` |
| `Count`, running below the limit | `board/trial.c:120` `board_trial_boot`, `:104` `count_start` |
| `Count`, other record states | `board/trial_boot_module.c:76` `trial_boot_module_start`, `logic/update_trial.c:61` `update_trial_boot`; a *rolled back* record is left in place (`trial_boot_module.c:89`) and flagged `USB_SETTLE_ROLLED_BACK` |
| `Count` → rollback copy, `RollbackMark` | `trial_boot_module.c:61` `roll_back` (slot 2 checked by `slot_copy_is_valid`, `logic/slot_copy.c:111`); record and restart at `trial_boot_module.c:71` |
| `RollbackClear` | `trial_boot_module.c:99` `settle`: `user_state_module_settle`, then the record deleted at `:105` |
| `CopyErase`, `CopyBody`, `CopyHead` | `logic/slot_copy.c:80` `slot_copy_run`: first chunk `0xFF` at `:85`, body at `:88`, first chunk last at `:94`; an erased source first chunk is refused at `:66`; overlay chunk at `board/overlay.h:11` (`0x43C000`, chunk 542 of 640) |
| `Pass` | `src/app/app.c:1761` → `board/trial.c:134` `hal_system_settle_user_state` → `board/user_state_module.c:19` → `src/settings/user_state.c:383` `user_state_run` |
| `BackupClear`, `BackupCopy`, `BackupName`, `BackupSlot` | `user_state.c:243` `user_state_backup`; slot choice `:180` `backup_slot` |
| `SettleFiles`, `AsideCopy`, `AsideRemove`, `ReturnCopy`, `ReturnRemove` | `user_state.c:300` `settle_file`, `:295` `move` |
| `WriteMark` | `user_state.c:374` `write_mark` |
| `Crash` | watchdog restart; `board/trial.c:204` `board_trial_fail` from `board/main.c:247` and `:383` |
| `Settle` | `board/trial.c:195` `board_trial_settle` from `board/main.c:398`; record write `trial.c:152` |
| `ChangeSettings`, `TornSettings` | `src/app/settings_store.c:87` `save_values` (refuses a later format at `:90`) |
| `Shutdown` | the owner switching the player off |
| `Session` | `board/main.c:90` `run_usb_session`; `board/trial.c:218` `board_trial_session_starting` |
| `Updater` | `board/usb_session.c:838` → `board/update.c:163` `update_after_usb`; already-installed check `logic/updater.c:302`; file removal rule `updater.c:308` |
| `Updater` both-slot branch, `UpdaterSlot1`, `UpdaterSlot2` | `board/trial_module.c:32` `trial_module_install` (`both` or slot 2 invalid) → `logic/updater_install.c:32` `updater_install`; order chosen by `slot_1_first` at `:26` |
| `UpdaterDelete`, `UpdaterBoth`, `UpdaterFile` | `trial_module.c:43`; `board/update.c:121`; `update.c:135` |
| `TrialRecord`, `TrialFailed` | `trial_module.c:15` `install_trial` |
| `SessionEnd`, `PromotionDue`, `PromotionCleared`, `RunningFromSlot1` | `board/trial.c:228` `board_trial_session_ended`; `trial_boot_module.c:144` `promotion` → `logic/update_trial.c:81` `update_trial_promotion`; `trial_boot_module.c:110` `running_from_slot_1` with the overlay loaded slot 1 first, `board/overlay.c:117` |
| `PromoteRecord`, `PromoteDelete` | `trial_boot_module.c:166` `trial_boot_module_session_end`, record and copy at `:182`, `end_trial` at `:156` |
| `PromoteClear` | `trial_boot_module.c:173`, `end_trial` |
| `Cut` | power loss between any two steps |

To check for drift, look up each function above in the current tree and compare its order of disk writes with the action sequence.

### Core 0 / core 1 hand-off: `CoreHandoff.tla`

**What is modelled.**

- The control block's `generation` and the two `StreamSlot`s: `state`, `generation`, `first_sample`, `samples` and `result`.
- The sample memory of each slot, as frame identities (generation, index).
- Core 1's `core1_stream_step`: generation check, slot-state check, held block, publish-when-short-of-room, decode, recheck, take, publish and finish.
- Core 0's `next_frame`: drop stale slots, look at the current slot, read the frames over several steps as the engine copies them, then release.
- Core 0 stopping and reopening a stream, which gives a new generation.

**Bounds.** A slot holds `Capacity = 4` frames, standing for 4,608. Decoded blocks are 1 to 3 frames ("largest" = 3, so a slot is published once fewer than 3 frames of room remain). A stream has `Total = 5` frames, and there are up to 2 streams. Core 1 decodes in place: a block that fits is written into the slot during `decode`. A block that does not fit is held, as copying decoders do.

**Memory ordering.**

- `core1_publish` is `__atomic_thread_fence(__ATOMIC_RELEASE)` and `core1_observe` is the acquire fence. Both compile to `dmb ish`: `arm-none-eabi-objdump -d` of `echo_device.elf` from a device build of `751de675` shows it in `next_frame` after the state read and as the first instruction of `release_frame`, where `advance_slot` is inlined.
- The shared block is uncached SRAM (the Cortex-M3 has no data cache), and every field is accessed through `volatile`.
- The model therefore treats a publish (fields, fence, `state = FULL`) and core 0's read of a full slot after the acquire fence as ordered.
- Two orderings are not fenced in the code, and the model makes each one a constant:
  - `LoadsReorder`: whether core 0's release of a slot (`state = EMPTY`, `advance_slot`) could overtake the engine's last read of that slot's samples. On the Cortex-M3, an in-order core whose loads complete before a later store issues, it cannot; the ARMv7-M architecture allows it for Normal memory. `ReleaseFenced` (`TRUE`: `core1_publish` at `core1_stream.c:426`, before the store) rules it out on any implementation.
  - `CoreOneWindowsShort`: core 1 checks the generation at `core1_stream.c:299` and again at `:269`. It then publishes at `:321` (filled slot), and inside `take` (`:239`, `:246`) after `append` and `copy_channels`, without checking again. When `TRUE`, the model lets core 0 stop or open a stream only while core 1 is outside that window. When `FALSE`, any interleaving is allowed. `CoreZeroChecksGeneration` (`TRUE`: `next_frame` empties a full slot of an earlier generation after its acquire fence, `:445`) makes the window harmless, so the hand-off is now also checked with `CoreOneWindowsShort = FALSE`.

    Core 0 cannot pause core 1. It can only hold or reset it (`core1_park`, `core1_stop`, `logic/core1_control.c:185`, `:195`). Core 1's own interrupts are a SysTick that sets the rest timer (`core1/startup.c`). The window is short only on the paths that publish directly. On the `take` paths, `copy_channels` first copies a whole block, up to 4,608 frames per channel, which takes milliseconds at core 1's lowest clock (25 MHz, divider 8). Whether core 0's stop, reopen and first `next_frame` can fit inside that copy has not been measured.

**Properties.**

| Property | Kind | Meaning |
|---|---|---|
| `NoSlotReadWhileWritten` | invariant | core 1 never writes the samples of a slot core 0 is reading |
| `NoStaleFrame` | invariant | core 0 never plays a slot of an earlier generation |
| `FramesInOrder` | invariant | core 0 plays frames 0, 1, 2, … of its generation with no gap, repeat or reordering |
| `NoFrameLost` | invariant | the end of a stream is reached only after every frame was played |
| deadlock freedom | TLC `CHECK_DEADLOCK` | the only state with no step is the explicit `Finished` stutter after the last stream |
| `TrackPlaysOut` | liveness | a stream being played reaches its end, unless core 0 stops it |
| `DecodedSlotPlayed` | liveness | a full slot of the current generation is eventually released |

Every core 0 and core 1 step has weak fairness. Stopping and opening streams are the user's choice and have none.

**Configurations and results.**

| Model | Config | Expect | Result | States generated (distinct) | Wall time |
|---|---|---|---|---|---|
| `core_handoff` | `CoreHandoff.cfg`: fences as built, short core 1 windows, in-order loads | passed | passed | 79,712 (38,368), depth 85 | 24 s |
| `core_handoff_witness` | `NeverPlaysOut`: both streams can play out in order | violated | violated as expected | 24,014 (12,348) | 6 s |
| `core_handoff_window` | `CoreOneWindowsShort = FALSE` | passed | passed | 89,474 (42,927), depth 85 | 30 s |
| `core_handoff_window_before` | same, `CoreZeroChecksGeneration = FALSE`, `NoStaleFrame` | violated: `NoStaleFrame` | violated as expected | 302 (190), 12 steps | 6 s |
| `core_handoff_reorder` | `LoadsReorder = TRUE` | passed | passed | 79,712 (38,368), depth 85 | 25 s |
| `core_handoff_reorder_before` | same, `ReleaseFenced = FALSE`, `NoSlotReadWhileWritten` | violated: `NoSlotReadWhileWritten` | violated as expected | 2,621 (1,382), 19 steps | 6 s |

**Model-to-code mapping** (`751de675`, `platform/device/logic/core1_stream.c` unless named).

| Model action | Code |
|---|---|
| `C1Check` | `core1_stream_step` `:295`, generation read `:299`, `adopt` `:131` |
| `C1Act` | slot state `:312`, held block `:315`, publish when short of room `:320` |
| `C1TakeHeld`, `C1Take` | `take` `:235`, `append` `:210`, `core_0_waiting` `:229`, `publish_slot` `:170` (fence `:177` before `state = FULL` `:178`), `finish` `:183` |
| `C1Decode` | `decode_next` `:264` with `next_block` `:255` (in-place output) |
| `C1Recheck` | `:269` |
| `C0Stop` | `core1_source_stop` `:349`, `generation = 0` `:353` |
| `C0Open` | fields, fence, `generation =` at `core1_m4a_source.c:57`, `core1_open_flac.c:37`, `core1_open_mp3.c:63`, `core1_ogg_source.c:42`, `core1_ape_source.c:20`, `core1_wma_source.c:24`, `core1_dsd_source.c:50` |
| `C0Drop` | `drop_stale_slots` `:372`, called at `:388` and `:439` |
| `C0Look` | `next_frame` `:431`, state `:441`, fence `:444`, generation check `:445` |
| `C0Read`, `C0LateRead` | `audio_engine.c:620` `copy_pending` reading `frame.left`/`right` |
| `C0Release` | `audio_engine.c:126` → `release_frame` `:476` → `advance_slot` `:424`, release fence `:426` |

`core1_pace` sets clocks and rest lengths only. It changes how long each step takes, not what the steps do. In the model it appears only as the fairness of core 1's steps: core 1 always runs its next step eventually.

**Not modelled.**

- The input ring (`written`, `released`, `acked_generation`). Core 0 fills it and core 1 reads it under the same fence pattern. Core 1 sets `released` (`:217`) after the decoder has read the bytes, with no barrier, which has the same in-order-core dependence as `LoadsReorder`.
- The Vorbis configuration hand-off.
- `hurry`.
- Trimming (`lead_trim`, `skip_before`, `play_end`).
- Decode errors other than end of stream.

## Results

Each expected violation documents one behaviour of the code. Every step of each trace maps to the code through the tables above. If a later change makes an expected violation pass, the check reports `unexpected-pass`; then turn it into an expected pass.

**Fixed: promotion of a part-written slot 1** (`update_promotes_partial_slot`, `update_write_failure`; `_before` with `FixPromotion = FALSE`). A trial install that erased slot 1's first chunk (`slot_copy.c:85`) and was cut off before the overlay chunk let the build in slot 2 count itself as the trial; a later ejected session with a stored write then promoted, because the overlay header still matched, and the copy erased slot 2 and took slot 1's erased first chunk as already copied, leaving no valid slot. Promotion now requires `slot_copy_is_valid` on slot 1; a due promotion with an invalid slot 1 deletes the record without writing either slot (`update_witness_clear` shows that branch is reachable), and the copy refuses an erased source first chunk. `test_a_cut_trial_install_is_never_promoted_over_the_build_in_slot_2` (`test_23_safe_update.py`) reproduces the sequence; it fails on `25be5c86` with slot 2's first chunk erased.

**Fixed: both-slot install over the only valid slot** (`update_both_request`; `_before` with `FixBothOrder = FALSE`). `updater_install` wrote slot 2 first even when slot 1 was invalid, so after a cut trial install a `BOTHSLOT.REQ` or FiiO install overwrote the only valid copy first. It now writes slot 1 first in that case (`test_after_a_cut_trial_install_the_developer_request_fills_slot_1_before_slot_2`, `test_03_updates.py`). `update_both_request_known_good` shows that the owner-requested case is otherwise as documented: the player keeps a good image unless the owner asked for both slots.

**Fixed: a cut right after a rollback restart skipped the continuity pass** (`update_lost_continuity_pass`; `_before` with `FixRecordAfterPass = FALSE`). The restored build deleted the *rolled back* record in `board_trial_boot`, before the pass inside `app_start`. The record now stays until the pass has returned, and `RollbackPassDone` checks that the app never runs with it still in place (`test_a_cut_after_a_rollback_restart_still_runs_the_continuity_pass`, `test_23_safe_update.py`, cuts at `app_start`).

**Fixed: a slot published under the previous generation could be played** (`core_handoff_window`; `_before` with `CoreZeroChecksGeneration = FALSE`). Core 1 could pass its generation check (`:269`), core 0 stop and reopen the stream and drop stale slots, core 1 publish with the old generation, and core 0 play that slot. `next_frame` now compares the slot's generation after its acquire fence. The race needs core 1 to publish between core 0's in-loop drop and its state read; no host or emulator seam reaches that window, so it is shown by TLC only.

**Fixed: the release store and the last sample loads** (`core_handoff_reorder`; `_before` with `ReleaseFenced = FALSE`). Core 0 releases a slot after a release fence, so the store cannot overtake its last sample loads on any ARMv7-M implementation.

**Known limitation: an update after an interrupted promotion takes the both-slot path** (`update_two_images`, expected violation of `KnownGoodKept`).

1. A promotion copy (slot 1 → slot 2) is cut off, so slot 2's header is invalid.
2. The next update sees slot 2 invalid and installs the old way, into both slots (`trial_module.c:39`). Slot 1 is valid, so slot 2 is written first.
3. The confirmed build in slot 1 is overwritten, so a broken update leaves no image to roll back to.

`device-install.md` (Both slots at once) states this. `update_two_images_other` shows that every other property holds with two updates.

**A failed rollback copy waits for the next start** (`update_write_failure_liveness`, expected violation of `TrialResolves`). With slot writes allowed to fail, a rollback copy that fails leaves the build running with the record still *running* and the trial flags clear, so no session promotes it and the record resolves only at a later start, which the owner may never make; with every rollback write failing, never. `update_write_failure` checks the safety properties under write failures, and they hold.

**Assumption checks.**

- `update_rom_slot1_only`: if the ROM does not start slot 2 for an invalid slot 1, any interrupted slot 1 write (install, rollback) leaves the player unbootable.
- `update_torn_sectors`: if a one-sector write can tear, a torn `settings.bin` with no backup yet leaves no valid settings, and a torn trial record silently ends the trial's protection.

**The owner's properties, as worded, that the design does not keep.**

- `update_trial_waits`: a trial build that keeps working stays on trial until a confirming USB session. Without one, the trial never ends, by design. Under the owner-confirms fairness, `TrialResolves` and `TrialEnds` hold.
- `update_settings_literal`: after a rollback, `settings.bin` keeps the trial build's changes when the format is unchanged. A later format is set aside, and that file starts from defaults. It is not restored to the pre-trial content. `PreviousSettingsRecoverable` holds: the pre-trial content stays in a named backup slot for Settings → Backups.
- `update_both_request_literal`: with `BOTHSLOT.REQ`, both slots hold the new build and there is no backup, as `device-install.md` says.

None of these results has been checked on the player.

**CBMC.** None of the passing harnesses found a defect within its bounds.
