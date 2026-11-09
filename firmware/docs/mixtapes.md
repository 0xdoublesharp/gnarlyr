# Saved cassette mixtapes

Music → Mixtapes saves the complete current queue, including duplicates, history and manually added songs. It also creates empty mixes, renames, replaces and deletes them. There are 32 named slots, each with up to 65,535 entries. Names hold up to 63 UTF-8 bytes. Existing M3U library playlists remain separate inputs.

Choose **Save current queue** or **New empty mixtape** to enter a name. Volume chooses a character, Play appends it, Menu erases the last Unicode character, and holding Play confirms. Back cancels. Selecting a saved mix offers **Play**, **Edit in Up Next**, **Replace with current queue**, **Rename** and **Delete**. Delete opens a confirmation with Keep selected initially.

**Edit in Up Next** loads an independent working queue and leaves it paused. The existing queue controls move, remove or add songs. Return to Mixtapes and choose **Replace with current queue** to publish those edits. Ordinary queue edits do not silently change the saved mix. Loading an empty mix leaves the current queue intact.

## Song identity

A saved entry records its full relative path and cue start/end milliseconds, rather than a transient library row number. Paths have at most 258 bytes, leaving room for the playback path's leading slash and terminator. Invalid, truncated or noncanonical paths are rejected. Two cue tracks in the same file remain distinct, and duplicate saved entries remain duplicates.

Loading builds a temporary index of the current library and resolves each saved identity. Missing songs and identities with multiple matching library rows are unavailable; the loader never picks an arbitrary duplicate. Available songs keep their saved order. An entirely unavailable mix leaves the old queue intact. Unavailable entries remain in the saved file so they can return after the missing files are restored.

Resolution borrows the second core's 384 KB scratch region while audio is stopped. The maximum index plus resolved sequence uses 393,214 bytes of 393,216. The scratch region and resolver remain alive until the resolved IDs have been copied into the pending current-order bank. Scratch is returned before restarting decoding. There is no heap or persistent full-library array in the application.

## Files and publication

All files live on the selected user volume under `/.echo/`. No raw sectors or firmware slots are involved.

- `mixes2.bin`: 6,144-byte catalog, containing two 3,072-byte banks.
- `MX00A.BIN` / `MX00B.BIN`, through slot 31: two data files per slot, allocated lazily to the required size.
- `order2.bin` and `session2.bin`: the current working queue and its root, described in [stored orders](stored-orders.md).

Saving first materializes the current queue and freezes it. The catalog checkpoints the currently published entry before an inactive data file is reused. Only an inactive file that needs more space is removed and recreated. The writer verifies the new data, commits its header, then publishes a catalog entry naming its exact generation and bank. Cancellation or a failed data write leaves the previous published entry. Rename and deletion change the catalog only. Reusing a deleted slot checkpoints its tombstone before touching old data.

A catalog readback that cannot establish publication produces an uncertain result and requires reopening. The UI reloads the catalog before showing its contents or starting another operation. If the newest selected data file is corrupt or missing and the older catalog names a different readable generation for that slot, loading validates that generation and repairs only the selected entry. Other latest catalog entries remain intact. Two banks retain a bounded previous state, not an archive of every edition.

Loading copies the resolved sequence into the spare working-order bank and publishes a session root before replacing the live player state. Failed publication retains the previous live queue. Loading, saving and cancellation preserve the old playback position on failure. Play starts a successful new queue; Edit leaves it paused. Back cancels, and shutdown, scanning and output release cancel before saving or borrowing other buffers. Bluetooth playback commands wait until the job finishes.

Core data reads/writes are at most 512 bytes. Application updates run at most 16 core steps or an observed 8 ms between steps before yielding. Initial file allocation and catalog reopening still use synchronous filesystem operations. Physical power-loss ordering and latency require device testing.

## Version 1 encodings

Each catalog bank has a 512-byte header followed by 32 entries of 80 bytes. Header fields are magic `MXCA`, little-endian version at byte 4, slot count at 6, generation at 8, payload byte count at 12, payload FNV-1a at 16 and header FNV-1a at 20. The header hash covers bytes 0–19. Entry fields are the 64-byte terminated name, data generation at 64, count at 68, data bank at 70, active byte at 71, payload bytes at 72 and zero reserved word at 76. An inactive entry retains its data generation but has an empty name, count and payload.

Each data file has a 512-byte header with magic `MXTP`, version at 4, count at 6, data generation at 8, payload length at 12, payload FNV-1a at 16 and header FNV-1a at 20. Records are a 16-bit path byte count, 32-bit cue start, 32-bit cue end and the unterminated path bytes. End zero means the file end. An empty mix has a valid header and an empty payload. These checks detect corruption and are not authentication.

## Memory and validation

Mixtape storage, resolution and screens run in the resident module, now bounded to 16 KB at `0x0308F000`. The top entry uses ABI 3, with 13 function pointers, including backend and screen API getters. Module bytes, entry targets and the main-code CRC are checked before execution. The glyph arena starts at `0x03093000` and has 48 KB; decoder and rescan regions remain unchanged. The high-RAM diagnostic excludes the resident module and tests the remaining 132 KB.

Host tests cover full-capacity/path limits, torn writes, cancellation, readback uncertainty, replacement, rename/delete/reuse, missing and ambiguous identities, duplicate entries, rescans and player restart. A native-image test drives the buttons through save and Edit in Up Next on a real FAT implementation, verifies twelve cue identities and cold-boots the saved working queue. These checks establish software behavior; the owner's recovery and physical-device validation remain pending.

## Follow-up

The existing current-session library fingerprint covers section layout rather than track contents. Saved mixtapes resolve by path/cue and explicitly pass a same-layout reorder test. Audit current-session restoration separately across library rescans before treating the full firmware goal as complete.
