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
python3 tools/firmware.py formal --list                 # names, without running anything
python3 tools/firmware.py formal --all                  # also the opt-in harnesses (hours)
python3 tools/firmware.py formal --cover --harness NAME # blocks a CBMC harness reaches
```

`python3 tools/formal.py` takes the same options. The entry point uses only Python's standard library and needs neither the ARM toolchain nor a firmware build. It stops if `/Volumes/Scratch` is not mounted.

Each run writes to `/Volumes/Scratch/SnowskyEcho/reports/formal/<worktree>/<date-time>/`:

- `summary.md` and `summary.json`: one row per check.
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

The figures are CPU seconds of the checker (`formal.py` measures its child processes). They come from one run on the development Mac (12 cores, Apple Silicon) while it was shared with other builds at load averages of about 8 to 15. The opt-in results and the reachability figures below come from the first runs at `bb0cb274`.

| Harness | Entry points | Bound | Unwind | Mode | Properties | Result | CPU |
|---|---|---|---|---|---|---|---|
| `canary` | planted faults in harness code | 16 B | 18 | complete, with `--uninitialized-check` | 57 | fails on exactly its 2 planted properties, as intended | 0 s |
| `library_database` | `library_open`, `library_read_*`, `library_album_key` | 160 B, fresh | 40 | complete, unsigned overflow on | 895 | passed | 183 s |
| `scan_record` | `music_record_read`, `music_check_*` | 528 B | 26 | complete | 719 | passed | 1 s |
| `tags_vorbis_comment` | `tags_read_vorbis_comment` | 64 B, fresh | 4 | depth-bounded | 4,478 | passed | 121 s |
| `playlist_reader` | `playlist_reader_open`, `_next` | 64 B, fresh | 4 | depth-bounded | 374 | passed | 267 s |
| `settings_codec` | `settings_decode`, `_encode`, `settings_buttons_decode`, `settings_file_newer` | 96 B | 98 | complete | 2,567 | passed | 18 s |
| `playback_store` | `store_pair_open`, `store_load`, `store_scan_next` | 1,100 B, fresh | 514 | complete | 1,030 | passed | 39 s |
| `user_state_records` | `user_state_list_decode`, `_mark_request`, `_age`, `update_trial_decode` | 44 B | 24 | complete | 1,394 | passed | 1 s |
| `language_pack` | `i18n_attach`, `i18n_pack_segment`, `i18n_attach_segment`, `i18n_text` | 128 B pack, 32 B module | 132 | complete | 859 | passed | 6 s |
| `language_loader` | `language_load`, `language_use_module` | 256 B, fresh; 192 B memory | 20 | complete | 538 | passed | 50 s |

Five harnesses are opt-in (`"default": false`). `formal.py` skips them unless they are named with `--harness` or the run uses `--all`. None has a useful verdict yet. The complete runs and the depth-bounded runs at unwind 12 stopped at a 30-minute wall limit after 10 to 18 CPU-minutes.

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
- **`theme_pack_v1` (`.gtheme`, `DSTP` version 1, opt-in, no verdict yet).** `theme_pack_open` on up to 640 bytes (fresh): the 512-byte header, the asset rows, the CRC walk and the signature trailer. The assertions are that every accepted asset lies inside the file and that the name and version end inside their fields. The palette decode runs, with its FNV stubbed.

### Not covered, and seams worth adding

- **Above the bound and the unwind limit.** A complete pass says nothing about files larger than its bound. A depth-bounded pass also says nothing about paths whose loops run longer. That leaves the following to the fuzzers and unit tests:
  - the ID3v2, FLAC, MP4 and CUE readers, which have no verdict at all;
  - the theme pack past its header check;
  - the Vorbis comment and playlist readers on paths where a loop runs more than three times (unwind 4 allows three iterations);
  - multi-chunk reads.
- **Not driven.**
  - The Ogg page walk, and the ASF, APE, WAV/AIFF and DSD tag readers.
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
  - Split the tag readers' per-frame handlers (`tags_id3.c`, `tags_vorbis.c`, `tags_mp4.c`) from their walking loops behind a function a harness can call on one frame. A harness could then check each handler completely and the walk separately.
  - Give `cue_sheet` a public entry point that handles one line.
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
| `C0Open` | fields, fence, `generation =` at `core1_open.c:57`, `core1_open_flac.c:37`, `core1_open_mp3.c:63`, `core1_ogg_source.c:42`, `core1_ape_source.c:20`, `core1_wma_source.c:24`, `core1_dsd_source.c:50` |
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
