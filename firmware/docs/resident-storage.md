# Resident storage code

The main boot-ROM code window stays at its original size. Persistent queue/session helpers and saved-mixtape storage/screens run from a separately loaded, read-only module in unused high RAM. The simulator and device use the same C implementations and on-disk formats. The host simulator links those same implementations directly.

## Memory and image layout

- Main code remains at `0x03050000` with its original `0x2666C`-byte load budget.
- Decoder workspace occupies 73,808 bytes starting at `0x0307A000`.
- The temporary rescan overlay starts at `0x0307A000` and is limited to `0x15000` bytes, ending before `0x0308F000`. The current overlay is 84,764 bytes.
- Resident storage code has `0x4000` bytes at `0x0308F000`–`0x03092FFF`, including its header and entry table.
- The glyph arena has 48 KB starting at `0x03093000`.

The linker enforces all three high-RAM regions. Rescan loading checks its smaller capacity before reading. The High RAM pattern diagnostic skips the resident region while checking the remaining 132 KB, then restores the glyph reader. A native ARM check exercises the diagnostic and compares the resident module byte-for-byte afterward. Source and emulator checks do not establish operation on the owner's physical player.

The resident image lives at firmware offset `0x218000`, covered by the normal image trailer CRC and both firmware-slot writes. Glyph placement excludes its entire reserved `0x4000`-byte range, even when the current module is smaller. Free image space released by the smaller rescan region is available to glyph placement. The boot-ROM scatter table does not gain another row, and its fw1-end convention stays unchanged.

## Loading and ABI

`board_storage_code_start` runs before application initialization. It reads bounded sectors from firmware slot 1 and then slot 2 if necessary, selecting a module whose CRC binding matches the currently executing main code. It checks the header, size, payload CRC, ABI version, exact entry-table size and every Thumb function pointer's bounds. Memory barriers precede publishing the ready flag. Failed loading leaves storage operations unavailable through the main-code wrappers; an unvalidated entry is never called. The rest of the player can reach its normal UI, allowing recovery through the existing flow.

The 16-byte image header is magic `ESTO`, little-endian total size, payload CRC and CRC of the main `.code` bytes. CRCs use the existing RKnano image algorithm. The payload begins with `SAPI`, ABI version 3 and table size 64, followed by thirteen 32-bit Thumb function pointers. The pointers cover eight track-order operations, three session-journal operations, and getters for the mixtape backend and screen APIs. New ABI layouts require a version change.

The module is linked against the main ELF's symbols. Its public implementations receive a `resident_` prefix so they cannot accidentally resolve back to the main forwarding functions. Packaging compares imported global addresses against the supplied main ELF and refuses a mismatched pair. The runtime main-code CRC also protects against selecting a module from the wrong firmware slot. `make_image.py` requires `--storage-code` when the main ELF contains the resident loader.

The module owns no writable globals, heap or BSS. Callers retain the owners, jobs, callbacks and buffers in their existing locations. Its memory remains resident across queue editing, playback, rescans and the High RAM diagnostic. Each main-code wrapper preserves the existing function signatures and fails without calling the module when loading was unsuccessful.

## Saved mixtapes

Named cassette mixtapes now use full paths and cue boundaries, a two-bank catalog and two data files per slot. Loading resolves those identities into the working queue. Save, replace, rename and delete publish catalog changes explicitly. Existing M3U imports remain supported. See [saved mixtapes](mixtapes.md) for the current controls, formats and memory lifetimes.
