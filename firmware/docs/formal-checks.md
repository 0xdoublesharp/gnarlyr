# Formal checks

Two kinds of formal check sit beside the tests:

- **CBMC harnesses** run the C code that reads files from the card and internal storage on every possible file content up to a stated size. They check memory safety and a few format properties.
- **TLA+ models** describe the update, trial and rollback path, and the core 0 / core 1 audio hand-off, as state machines. TLC explores every interleaving within stated bounds.

The checks run only when someone asks for them. They are not part of `python3 tools/firmware.py test`, the `host` or `smoke` groups, the CTest registration, or the push and pull-request CI. The only CI entry is the manually started `Formal checks` workflow (`.github/workflows/formal.yml`).

Everything here was checked against `main` at `bb0cb274`. The results below come from runs on that tree.

## Running the checks

```sh
python3 tools/firmware.py formal                        # every harness and model
python3 tools/firmware.py formal --cbmc                 # CBMC harnesses only
python3 tools/firmware.py formal --tla                  # TLA+ models only
python3 tools/firmware.py formal --harness core_handoff # one harness or model
python3 tools/firmware.py formal --list                 # names, without running anything
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

The CI workflow installs the same CBMC release, checks its SHA-256, installs OpenJDK 21 and runs `--fetch-tla`. It uploads the report folder as an artifact. You can choose `all`, `cbmc` or `tla`, and optionally one harness.

`tests/test_formal.py` covers the entry point with a stubbed tool runner, so `python3 -m unittest discover -s tests` stays fast and runs no checker.

## Layout

| Path | Contents |
|---|---|
| `firmware/formal/checks.json` | The manifest: CBMC flags, each harness's sources, stubs, bounds and unwind limit, and each TLA+ model's spec, config and expected result |
| `firmware/formal/cbmc/harnesses/` | One harness file per reader family; the entry point is always `harness()` |
| `firmware/formal/cbmc/stubs/` | `card.c` (the nondeterministic file), `hash.c` (checksum seam), `hal.c` (file, version and language-memory HAL), `theme.c` (signature seam) |
| `firmware/formal/cbmc/catalogue/core/strings.h` | A four-string, three-segment catalogue that replaces the generated one for the language harness only |
| `firmware/formal/tla/` | `UpdateRollback.tla`, `CoreHandoff.tla` and one `.cfg` per checked configuration |
| `tools/formal.py`, `tests/test_formal.py` | The entry point and its unit test |

The harness C follows the firmware rules: no comments, formatted with `.clang-format`. No firmware name, identifier or folder appears in any of these files. The C reads them from `FIRMWARE_*` definitions taken from `identity.json`.

## CBMC harnesses

### Common set-up

- **Target.** `goto-cc -m32 -funsigned-char`: 32-bit `int`, `long` and pointers, little-endian, unsigned `char`, as on the Cortex-M3. CBMC labels the goto program's library as i386, but the type widths and byte order are the ones that matter, and they match.
- **Checks on every harness:** `--bounds-check --pointer-check --pointer-overflow-check --signed-overflow-check --div-by-zero-check --undefined-shift-check`, plus `--unwinding-assertions`. With unwinding assertions, a loop that could run past the unwind limit within the input bound fails the check instead of being cut short silently. CBMC 6 also turns a call to a function with no body into a failing property, and `formal.py` reports such a call as an `error`.
- **Unsigned overflow.** `--unsigned-overflow-check` is off globally because firmware code wraps on purpose: store generations, the language row check `size - 1 - 2n`, and the hash. It is on for `library_database` and `theme_pack_v1`, where every offset and size sum must stay below the file size and a wrap would be a bug.
- **Uninitialised memory.** CBMC itself gives uninitialised memory an arbitrary value, so every property is checked for every content such memory could have. It does not report the read itself. `goto-instrument --uninitialized-check` would, but it tracks only direct assignments to locals. Every reader here fills its buffers through a callee (`hal_file_open(&file)`, `read_source(..., header, ...)`), so that check reports each of them falsely. It runs on the canary only, to show it works.
- **The file.** `stubs/card.c` stands for the file on the card. The file's size is nondeterministic up to `FORMAL_CARD_BYTES`. A read:
  - may fail at any time, in which case it overwrites the caller's buffer with arbitrary bytes, as a part-done read would;
  - fails if it reaches past the size, as `fat_read` does;
  - otherwise copies the bytes;
  - always asserts that the caller's buffer can hold the requested length.

  The bytes come from one nondeterministic array, so two reads of the same offset agree. Harnesses marked *fresh* below set `FORMAL_CARD_FRESH`. Each successful read then returns new arbitrary bytes instead, which keeps the solver fast. That over-approximates the file, so a pass is still a proof for every real file. A failure found in fresh mode must be replayed with consistent reads before it counts.
- **Checksum seams.** `hash_update` (FNV-1a) returns an arbitrary value but asserts that the whole span it would hash is readable. `hash_store` and `hash_load` keep their byte layout. The theme pack's `theme_pack_crc` is given a nondeterministic body with `goto-instrument --generate-function-body`. `theme_signature_inspect` returns an arbitrary verdict. As a result, every checksum comparison both passes and fails, so the code behind each checksum is reached for every content. The checksum code itself is not checked.

### Results

Run times are wall-clock on the development Mac (12 cores, Apple Silicon) while it was shared with other builds at load averages between 110 and 190. An unloaded machine is faster.

| Harness | Entry points | Bound | Unwind | Properties | Result | Time |
|---|---|---|---|---|---|---|
RESULTS_TABLE

### What each harness proves

For every file content up to the bound, every listed entry point stays inside its buffers and the file, with no signed overflow, undefined shift or division by zero, and every loop ends within the unwind limit.

- **`canary`.** A planted overflow and an uninitialised read in harness code. It must fail on exactly those two properties. If it passes, the set-up is broken.
- **`library_database` (`library.db`, `DSDB` version 2).** `library_open` (header, section table, the content fingerprint walk) on a file of up to 160 bytes (fresh), and then on any accepted file `library_read_track`, `_album`, `_group` (any section), `_folder`, `_index` (any section) and `_string` (capacity 0 to 16) at any index, and `library_album_key`. Asserts that every accepted section lies inside the file, music counts never exceed section counts, and strings end inside their buffer. The header alone is 128 bytes, so this leaves up to 32 bytes of section data. The playlist and hidden-track overlays (`Library.extra`, `Library.hidden`) are not set, because no file sets them.
- **`scan_record` (`scan.bin`, `DSS2`).** `music_record_read`, `music_check_firmware_is_new`, `music_check_covers_pending`, `music_check_interrupted_scans`, `music_check_generation_moved` and `music_record_settled` on a file of up to 528 bytes, which reaches the scan guard at offset 512, with an arbitrary firmware version string. The task named one reader, "library/scan.bin, format 2, `DSS2`". The code has two: the library database (`DSDB` version 2) and the music check record (`DSS2`). Each has its own harness.
- **`tags_id3v2`, `tags_flac`, `tags_mp4`.** `track_tags_read` for MP3 (ID3v2, then the MPEG frame scan and ID3v1), FLAC (metadata blocks, Vorbis comment, picture) and MP4 (atom walk, `ilst`). Every tag text ends inside its 256-byte field, and a raw picture lies inside the file. Bounds are 48, 64 and 64 bytes. 64 bytes is enough for `fLaC`, a STREAMINFO block and a short comment block, and for an `ftyp`/`moov`/`udta`/`meta`/`ilst` chain only with a minimal tree. Ogg, ASF, APE, WAV, AIFF and DSD readers are compiled in but not driven.
- **`tags_vorbis_comment`.** `tags_read_vorbis_comment` directly, at any offset and limit inside a 48-byte file, for both Vorbis and Opus framing. This reaches the comment parser, the key table, the replay-gain fields and the base64 picture probe without the FLAC wrapper.
- **`cue_sheet`.** `cue_sheet_open` and up to three `cue_sheet_next` on a 40-byte file. Titles, performers and the album fields end inside their fields. `playlist_reader` is the line reader underneath.
- **`playlist_reader`.** `playlist_reader_open` and up to four `playlist_reader_next` on a 40-byte file with an entry buffer of 1 to 12 bytes. Every entry ends inside its buffer. This covers UTF-8 detection, Windows-1252 conversion and the start directive.
- **`settings_codec` (`settings.bin`, `buttons.bin`).** `settings_decode`, `settings_encode` of the result, `settings_buttons_decode` and `settings_file_newer` on 0 to 96 bytes of arbitrary content. A decoded language code ends inside its field, and the preset is in range.
- **`playback_store` (the two-bank `StoreFile` format).** `store_pair_open` with an arbitrary bank size, `store_load` at any offset, and `store_scan_next` at any position, on a 1,100-byte file (fresh). An inline payload never exceeds its block. This covers `order.bin`, `session.bin`, `mixes.bin`, `moments.bin`, `track_keys.bin`, `listens.bin`, `recap.bin`, `theme.bin` and the other store files at the block level. Each format's payload decoder is not covered.
- **`user_state_records`.** `user_state_list_decode` (`backup/list.bin`), `user_state_mark_request` (`continuity.bin`), `user_state_age` for every catalogue entry, and `update_trial_decode` (`trial.bin`) on arbitrary bytes. Build names end inside their fields, the newest slot is a slot or none, a restore request names a slot or none, and a trial state is known.
- **`language_pack`.** `language_load` with an arbitrary code, then `language_use_module` for any module, then `i18n_text` for any string. Packs come from a file or a built-in copy, both of up to 160 bytes, into 160 bytes of language memory. Every looked-up text ends inside the attached pack. The generated catalogue (910 strings in 7 segments) is replaced by a catalogue of 4 strings in 3 segments, with the same header and row layout. Without that, the smallest valid pack would be over 2 KB, out of reach. The parser's logic does not depend on the catalogue's size, but the original counts were not checked.
- **`theme_pack_v1` (`.gtheme`, `DSTP` version 1).** `theme_pack_open` on up to 640 bytes (fresh): the 512-byte header, up to 8 asset rows, the CRC walk and the signature trailer. Every accepted asset lies inside the file, and the name and version end inside their fields. The palette decode runs, with its FNV stubbed.

### Not covered, and seams worth adding

- **Above the bound.** A pass says nothing about files larger than the bound. In particular, the long-file paths (multi-chunk playlist reads, more than 8 theme assets, many FLAC blocks) are covered only by the fuzzers and unit tests.
- **Not driven.** The Ogg page walk, ASF, APE, WAV/AIFF and DSD tag readers, the MP4 chapter and timed-metadata readers, the cover image decoders (JPEG, PNG), `fat`/`exfat`, the headphone and parametric files, bookmarks and personal tags, `covers.bin`, the library gain table, and the rescan job's own read of `scan.bin`. The Ogg reader is the next one to add. A 128-byte bound with fresh reads should reach the second page.
- **Checksum code.** FNV, the theme CRC-32 and the signature check are replaced at the seams described above.
- **The language catalogue** at its real size.
- **Seams.** These would make bigger bounds affordable, but the product code was not changed to fit the harnesses:
  - Expose `library_content`'s block size as a compile-time option, so the walk can use a 32-byte block in the harness.
  - A pure decode function for the settings and store headers that takes a precomputed checksum, so the real FNV can stay in.
  - A `ByteSource`-based variant of `music_check_read_at`, so `scan.bin` needs no HAL stub.

## TLA+ models

TLC runs one worker for every model. The 1.7.4 release fixed an unsound liveness check with several workers (tlaplus issue 971); one worker avoids the question. No configuration uses symmetry.

Each model whose expected result is a violation names the property in `checks.json`. `formal.py` fails the check if TLC reports any other property, so a `TypeOK` slip cannot pass for an expected result. TLC stops at the first violation it finds. The configurations that expect one therefore check only that property, and a companion configuration checks the rest. The canary names its two expected CBMC properties the same way.

The wall times in these tables come from one sequential `formal.py --tla` run on the development Mac at load averages above 100.

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
| `RollbackRestoresImage` | invariant | after a rollback start, slot 1 holds slot 2's image |
| `RollbackRestoresSettings` | invariant (as worded) | after a rollback, `settings.bin` equals what the previous build had when the trial build first started |
| `PreviousSettingsRecoverable` | invariant (design intent) | after a rollback, that content is in `settings.bin` or in a named backup slot, so Settings → Backups can restore it |
| `RestoredBuildCanSave` | invariant | a build running with no trial can write its settings (the file is not of a later format) |
| `TrialResolves` | liveness | a running trial eventually leaves *running*: it is promoted, rolled back, or replaced by another install |
| `TrialEnds` | liveness | a trial record that is *running* or *promoting* is eventually deleted |

The base spec has weak fairness on power-on, on every firmware step, on a broken build's crash and on settling. `SpecOwnerConfirms` adds strong fairness on a confirming USB session.

**Configurations and results.**

| Model | Config | Expect | Result | States generated (distinct) | Wall time |
|---|---|---|---|---|---|
| `update_rollback` | `UpdateRollback.cfg`: one update, 2 cuts, 2 copies, ROM falls back, atomic sectors, owner confirms | passed | passed | 82,381 (53,901), depth 128 | 30 s |
| `update_witness_rollback` | same constants, `RollbackWitness`: a rollback start is reachable | violated | violated as expected | 4,161 (2,960) | 7 s |
| `update_witness_promotion` | `PromotionWitness`: a promotion is reachable | violated | violated as expected | 2,707 (2,071) | 7 s |
| `update_promotes_partial_slot` | 1 cut, the owner may delete the update file | violated: `AlwaysBootable` | violated as expected | 1,052 (864), 17 steps | 8 s |
| `update_write_failure` | 1 cut, slot writes may fail | violated: `AlwaysBootable` | violated as expected | 2,210 (1,583), 20 steps | 8 s |
| `update_both_request` | 1 cut, `BOTHSLOT.REQ` allowed | violated: `AlwaysBootable` | violated as expected | 1,315 (1,010), 15 steps | 7 s |
| `update_both_request_known_good` | same, `KnownGoodKept` only | passed | passed | 78,668 (49,195) | 9 s |
| `update_both_request_literal` | same, `PreviousImageKept` | violated | violated as expected | 685 (456) | 7 s |
| `update_two_images` | updates `A` and `B`, 1 cut | violated: `KnownGoodKept` | violated as expected | 75,633 (53,375), 35 steps | 17 s |
| `update_two_images_other` | same, every other invariant and `TrialResolves` | passed | passed | 451,734 (322,268), depth 127 | 63 s |
| `update_trial_waits` | base spec, no owner fairness | violated: liveness | violated as expected | 22,350 (15,716) | 10 s |
| `update_settings_literal` | 1 cut, `RollbackRestoresSettings` | violated | violated as expected | 7,485 (5,116) | 8 s |
| `update_lost_continuity_pass` | 2 cuts, `RestoredBuildCanSave` | violated | violated as expected | 16,082 (10,146) | 8 s |
| `update_torn_sectors` | 1 cut, sector writes may tear | violated: `SettingsAvailable` | violated as expected | 29 (25), 5 steps | 7 s |
| `update_rom_slot1_only` | 1 cut, the ROM starts only slot 1 | violated: `AlwaysBootable` | violated as expected | 157 (105), 8 steps | 7 s |

**Model-to-code mapping** (`bb0cb274`).

| Model action | Code |
|---|---|
| `PowerOn` | boot ROM slot choice (assumption, `architecture.md`, Firmware slots and the boot ROM); `board/main.c:368` calls `board_trial_boot` |
| `Count`, running below the limit | `board/trial.c:120` `board_trial_boot`, `:104` `count_start` |
| `Count`, other record states | `board/trial_boot_module.c:75` `trial_boot_module_start`, `logic/update_trial.c:61` `update_trial_boot`; deleting the *rolled back* record at `trial_boot_module.c:87` |
| `Count` → rollback copy, `RollbackMark` | `trial_boot_module.c:60` `roll_back` (slot 2 checked by `slot_copy_is_valid`, `logic/slot_copy.c:101`); record and restart at `trial_boot_module.c:70` |
| `CopyErase`, `CopyBody`, `CopyHead` | `logic/slot_copy.c:70` `slot_copy_run`: first chunk `0xFF` at `:75`, body at `:78`, first chunk last at `:84`; overlay chunk at `board/overlay.h:11` (`0x43C000`, chunk 542 of 640) |
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
| `Updater` both-slot branch, `UpdaterSlot1` | `board/trial_module.c:32` `trial_module_install` (`both` or slot 2 invalid) → `logic/updater_install.c:26` `updater_install` |
| `UpdaterDelete`, `UpdaterBoth`, `UpdaterFile` | `trial_module.c:43`; `board/update.c:121`; `update.c:135` |
| `TrialRecord`, `TrialFailed` | `trial_module.c:15` `install_trial` |
| `SessionEnd`, `PromotionDue`, `RunningFromSlot1` | `board/trial.c:228` `board_trial_session_ended`; `trial_boot_module.c:131` `promotion_due`; `trial_boot_module.c:97` `running_from_slot_1` with the overlay loaded slot 1 first, `board/overlay.c:117` |
| `PromoteRecord`, `PromoteDelete` | `trial_boot_module.c:141` `trial_boot_module_session_end`, record and copy at `:149` |
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

- `core1_publish` is `__atomic_thread_fence(__ATOMIC_RELEASE)` and `core1_observe` is the acquire fence. Both compile to `dmb ish`: `arm-none-eabi-objdump -d` of `core1_stream.c.obj` in an existing device build on Scratch shows the instruction at both. That build was not rebuilt from `bb0cb274`.
- The shared block is uncached SRAM (the Cortex-M3 has no data cache), and every field is accessed through `volatile`.
- The model therefore treats a publish (fields, fence, `state = FULL`) and core 0's read of a full slot after the acquire fence as ordered.
- Two orderings are not fenced in the code, and the model makes each one a constant:
  - `LoadsReorder`: core 0's release of a slot (`state = EMPTY`, `advance_slot`) has no barrier after the engine's last read of that slot's samples. On the Cortex-M3, an in-order core whose loads complete before a later store issues, this is safe. The ARMv7-M architecture allows the reorder for Normal memory.
  - `CoreOneWindowsShort`: core 1 checks the generation at `core1_stream.c:299` and again at `:269`. It then publishes at `:321` (filled slot), and inside `take` (`:239`, `:246`) after `append` and `copy_channels`, without checking again. When `TRUE`, the model lets core 0 stop or open a stream only while core 1 is outside that window. When `FALSE`, any interleaving is allowed.

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
| `core_handoff` | `CoreHandoff.cfg`: fences as built, short core 1 windows, in-order loads | passed | passed | 79,712 (38,368), depth 85 | 34 s |
| `core_handoff_witness` | `NeverPlaysOut`: both streams can play out in order | violated | violated as expected | 24,014 (12,348) | 8 s |
| `core_handoff_window` | `CoreOneWindowsShort = FALSE` | violated: `NoStaleFrame` | violated as expected | 302 (190), 12 steps | 8 s |
| `core_handoff_reorder` | `LoadsReorder = TRUE` | violated: `NoSlotReadWhileWritten` | violated as expected | 2,621 (1,382), 19 steps | 8 s |

**Model-to-code mapping** (`bb0cb274`, `platform/device/logic/core1_stream.c` unless named).

| Model action | Code |
|---|---|
| `C1Check` | `core1_stream_step` `:295`, generation read `:299`, `adopt` `:131` |
| `C1Act` | slot state `:312`, held block `:315`, publish when short of room `:320` |
| `C1TakeHeld`, `C1Take` | `take` `:235`, `append` `:210`, `core_0_waiting` `:229`, `publish_slot` `:170` (fence `:177` before `state = FULL` `:178`), `finish` `:183` |
| `C1Decode` | `decode_next` `:264` with `next_block` `:255` (in-place output) |
| `C1Recheck` | `:269` |
| `C0Stop` | `core1_source_stop` `:349`, `generation = 0` `:353` |
| `C0Open` | fields, fence, `generation =` at `core1_open.c:57`, `core1_open_flac.c:37`, `core1_open_mp3.c:63`, `core1_ogg_source.c:42`, `core1_ape_source.c:20`, `core1_wma_source.c:24`, `core1_dsd_source.c:50` |
| `C0Drop` | `drop_stale_slots` `:372`, called at `:388` and `:438` |
| `C0Look` | `next_frame` `:430`, state `:440`, fence `:443`; no generation check before `:444` |
| `C0Read`, `C0LateRead` | `audio_engine.c:620` `copy_pending` reading `frame.left`/`right` |
| `C0Release` | `audio_engine.c:126` → `release_frame` `:471` → `advance_slot` `:424` |

`core1_pace` sets clocks and rest lengths only. It changes how long each step takes, not what the steps do. In the model it appears only as the fairness of core 1's steps: core 1 always runs its next step eventually.

**Not modelled.**

- The input ring (`written`, `released`, `acked_generation`). Core 0 fills it and core 1 reads it under the same fence pattern. Core 1 sets `released` (`:217`) after the decoder has read the bytes, with no barrier, which has the same in-order-core dependence as `LoadsReorder`.
- The Vorbis configuration hand-off.
- `hurry`.
- Trimming (`lead_trim`, `skip_before`, `play_end`).
- Decode errors other than end of stream.

## Results at `bb0cb274`

Each expected violation documents one behaviour of the code at `bb0cb274`. Every step of each trace maps to the code through the tables above. None has been reproduced on the emulator or the player. If a fix lands, the matching configuration will report `unexpected-pass`; then turn it into an expected pass.

**`update_promotes_partial_slot` and `update_write_failure`: promotion can copy a part-written slot 1 over slot 2.**

1. A trial install erases slot 1's first chunk (`slot_copy.c:75`) and is cut off, by power loss or a failed write, before it reaches the USB overlay chunk at `0x43C000`. Chunks are 8 KiB, so that is chunk 542 of 640.
2. The ROM starts slot 2. This depends on `RomFallsBack`.
3. The previous build finds the *running* record and counts itself as the trial (`trial.c:104`).
4. A USB session then ends ejected with a stored internal write (`msc_write_proven`, `logic/msc.c:147`). Deleting the update file is such a write. If no update file is installed at the end of that session, because it was deleted or because the reinstall's first write failed, then:
   - `promotion_due` (`trial_boot_module.c:131`) holds. It checks the record and the session, and `running_from_slot_1` (`:97`) compares only the overlay's 16-byte header, which is still the running build's.
   - Nothing checks slot 1's header with `slot_copy_is_valid` before the copy at `:150`.
5. The copy erases slot 2, then copies slot 1's erased first chunk as unchanged (`slot_copy.c:60`). Both slots are left without a valid header.

The only code that writes the slots is the install (`trial_module.c`, `updater_install.c`), the rollback and the promotion (`trial_boot_module.c`). None of them repairs slot 1 first.

**`update_both_request`: the both-slot path writes slot 2 first even when slot 1 is invalid.**

`updater_install` (`updater_install.c:34`) always writes the file to slot 2, then copies slot 2 to slot 1. After an interrupted trial install, slot 2 holds the only bootable image. A `BOTHSLOT.REQ` install, or FiiO's image (`update.c:116`: a stock image always takes this path), then overwrites that image first. A power cut during that write leaves no valid slot. `update_both_request_known_good` shows that the owner-requested case is otherwise as documented: the player keeps a good image unless the owner asked for both slots.

**`update_two_images`: an update after an interrupted promotion takes the both-slot path.**

1. A promotion copy (slot 1 → slot 2) is cut off, so slot 2's header is invalid.
2. The next update sees slot 2 invalid and installs the old way, into both slots (`trial_module.c:39`, documented in `architecture.md`).
3. The confirmed build in slot 1 is overwritten, so a broken update leaves no image to roll back to.

`update_two_images_other` shows that every other property holds with two updates.

**`update_lost_continuity_pass`: a power cut right after a rollback restart can skip the continuity pass.**

1. The restored build deletes the *rolled back* record at `trial_boot_module.c:87`, called from `main.c:368`.
2. The continuity pass runs later, from `app.c:1761`.
3. A cut between the two leaves no record to set `USB_SETTLE_CHANGED`, so the pass never runs for that rollback:
   - a settings file in a later format is not set aside;
   - the restored build starts with defaults and does not save its settings (`settings_store.c:90`) until the next pass, at the next trial start or restore request;
   - the continuity mark still names the trial build;
   - "Update rolled back" is not shown.

**`core_handoff_window`: a slot published under the previous generation can be played.**

1. Core 1 passes its generation check (`core1_stream.c:269`).
2. Core 0 stops the stream (`:353`), opens a new one (`core1_open.c:57`) and drops stale slots (`:438`).
3. Core 1 then publishes its slot with the old generation (`publish_slot`, `:170`).
4. Core 0 sees the slot full at `:440` and plays it, because `next_frame` does not compare `slot->generation` with `source->generation`.

The effect is up to one slot of the previous stream or position. If that stale slot's `first_sample` is past the new `play_end`, the track ends early, and core 0's slot index falls out of step with core 1's. Whether this happens depends on the timing described under `CoreOneWindowsShort`.

**Assumption checks.**

- `update_rom_slot1_only`: if the ROM does not start slot 2 for an invalid slot 1, any interrupted slot 1 write (install, rollback) leaves the player unbootable.
- `update_torn_sectors`: if a one-sector write can tear, a torn `settings.bin` with no backup yet leaves no valid settings, and a torn trial record silently ends the trial's protection.
- `core_handoff_reorder`: if core 0's release store could overtake its last sample loads, core 1 could overwrite a slot that is still being read. This is safe on the Cortex-M3's in-order pipeline, but no barrier enforces it.

**The owner's properties, as worded, that the design does not keep.**

- `update_trial_waits`: a trial build that keeps working stays on trial until a confirming USB session. Without one, the trial never ends, by design. Under the owner-confirms fairness, `TrialResolves` and `TrialEnds` hold.
- `update_settings_literal`: after a rollback, `settings.bin` keeps the trial build's changes when the format is unchanged. A later format is set aside, and that file starts from defaults. It is not restored to the pre-trial content. `PreviousSettingsRecoverable` holds: the pre-trial content stays in a named backup slot for Settings → Backups.
- `update_both_request_literal`: with `BOTHSLOT.REQ`, both slots hold the new build and there is no backup, as `device-install.md` says.

**CBMC.** None of the passing harnesses found a defect within its bounds.
