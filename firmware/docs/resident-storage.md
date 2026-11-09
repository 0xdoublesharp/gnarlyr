# Resident storage code

The main boot-ROM code window stays at its original size. Persistent queue and session helpers now run from a separately loaded, read-only module in unused high RAM. Their C implementations and on-disk formats are unchanged. The host simulator links those same implementations directly.

## Memory and image layout

- Main code remains at `0x03050000` with its original `0x2666C`-byte load budget.
- Decoder workspace occupies 73,808 bytes starting at `0x0307A000`.
- The temporary rescan overlay starts at `0x0307A000` and is limited to `0x15000` bytes, ending before `0x0308F000`. The current overlay is 84,756 bytes.
- Resident storage code has `0x3000` bytes at `0x0308F000`–`0x03091FFF`. The current module is 3,912 bytes, including its header and entry table.
- The glyph arena starts at `0x03092000`. Its address and capacity are unchanged.

The linker enforces all three high-RAM regions. Rescan loading checks its smaller capacity before reading. The High RAM pattern diagnostic skips the resident region while checking the remaining 136 KB, then restores the glyph reader. A native ARM check exercises the diagnostic and compares the resident module byte-for-byte afterward. Source and emulator checks do not establish operation on the owner's physical player.

The resident image lives at firmware offset `0x218000`, covered by the normal image trailer CRC and both firmware-slot writes. Glyph placement excludes its entire reserved `0x3000`-byte range, even when the current module is smaller. Free image space released by the smaller rescan region is available to glyph placement. The boot-ROM scatter table does not gain another row, and its fw1-end convention stays unchanged.

## Loading and ABI

`board_storage_code_start` runs before application initialization. It reads bounded sectors from firmware slot 1 and then slot 2 if necessary, selecting a module whose CRC binding matches the currently executing main code. It checks the header, size, payload CRC, ABI version, exact entry-table size and every Thumb function pointer's bounds. Memory barriers precede publishing the ready flag. Failed loading leaves storage operations unavailable through the main-code wrappers; an unvalidated entry is never called. The rest of the player can reach its normal UI, allowing recovery through the existing flow.

The 16-byte image header is magic `ESTO`, little-endian total size, payload CRC and CRC of the main `.code` bytes. CRCs use the existing RKnano image algorithm. The payload begins with `SAPI`, ABI version 1 and table size 56, followed by eleven 32-bit Thumb function pointers. The pointers cover the eight track-order operations and three session-journal operations. New ABI layouts require a version change.

The module is linked against the main ELF's symbols. Its public implementations receive a `resident_` prefix so they cannot accidentally resolve back to the main forwarding functions. Packaging compares imported global addresses against the supplied main ELF and refuses a mismatched pair. The runtime main-code CRC also protects against selecting a module from the wrong firmware slot. `make_image.py` requires `--storage-code` when the main ELF contains the resident loader.

The module owns no writable globals, heap or BSS. Callers retain the owners, jobs, callbacks and buffers in their existing locations. Its memory remains resident across queue editing, playback, rescans and the High RAM diagnostic. Each main-code wrapper preserves the existing function signatures and fails without calling the module when loading was unsuccessful.

## Saved-playlist direction

This supplies code space for the next feature; saved-playlist controls are not implemented yet. Durable entries will use full audio paths plus cue start/end positions, preserving duplicate songs and distinguishing cue tracks sharing one file. A rescan can change library IDs without changing those identities. Missing songs must remain in saved data and be reported when loading; they must not silently bind to a different song.

The intended catalog uses bounded named slots and two verified records. Per-playlist data uses two files, growing only the inactive file when necessary through the existing FAT allocation/deletion logic. A verified catalog publication selects the new data generation. Loading resolves identities into a normal stored queue, where the existing move/remove/repeat controls apply. Save, replace, rename and delete publish catalog changes explicitly. Existing M3U imports remain supported.
