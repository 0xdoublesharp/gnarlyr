# Persistent track orders

`playback/track_order` provides the shared sequence store for edited queues and playlists. It supports up to 65,535 entries, including duplicate IDs, without retaining the sequence in RAM. The player now uses it for full queue editing, including generated album and shuffled queues. Named saved mixtapes now use a separate path/cue catalog; see [saved mixtapes](mixtapes.md).

## Ownership and API

The caller supplies a `TrackOrderConfig` with bounded read/write callbacks, the reserved file size, the identity-table fingerprint and its exclusive upper ID limit. The callbacks address one file, not a raw disk. A read or write must complete synchronously and report failure honestly; a failed write may have changed a prefix. Reads and writes never request more than 512 bytes. The application must reserve the full file before opening it because the current FAT writer cannot grow files.

Keep one writer per file and one job per writing owner. The player retains a frozen active owner while a pending copy writes the other bank; the root session journal determines when the pending generation becomes authoritative. This is a coordinated read view, not a second independent writer. Callbacks, input sequence and identity table must remain alive and stable throughout a job. Do not reinitialize an owner while its job is running. The busy flag excludes another job using the same owner; it does not coordinate independent objects, processes or USB mass-storage access.

`track_order_open` validates both banks, headers, IDs and complete payload checksums. It distinguishes no valid committed order from an I/O failure. No valid copy is not evidence that a named playlist was intentionally cleared; an intentionally empty list is a ready record with zero entries. A library/table mismatch rejects the stored IDs. A checksum-valid header with an unsupported version refuses editing rather than being overwritten as an empty list; future versions must retain the protected header prefix or use a different file path. A valid older bank is selected when the newer bank is corrupt; serial-number comparison handles generation wraparound. A bank that cannot be read produces an unreadable result rather than inviting an overwrite of unknown state.

`track_order_open_at` selects an exact generation after validation. The session journal uses this API so an unpublished newer data bank cannot silently replace the active queue.

`track_order_job_begin` starts copy, move, remove, insert or clear. It validates bounds before writing. Repeated `track_order_job_step` calls invalidate the inactive header, write at most 256 IDs, verify at most 256 IDs' bytes, and finally publish the new header. A source adapter can supply generated queue entries; input is read in contiguous runs where possible. The event loop must yield between steps and hold a stable source. Cancellation before publication preserves the previous committed bank.

The commit writes its header last and reads it back. A write callback that reports failure after completing a valid header can still produce a successful commit, because readback establishes the actual stored result. If header readback itself fails, the result is uncertain: the in-memory owner retains its old bank, sets `needs_reload`, and refuses another edit until reopened. The caller must not describe that result as definitely unsaved. A zero-initialized job is idle and safe to step or cancel; terminal jobs do not perform more I/O.

`track_order_read` reads bounded blocks from the validated active bank and checks ID bounds. Whole-payload checksum validation occurs on open and before publication, not on every individual row lookup. The two-bank protocol is tested against interrupted and torn writes; actual controller ordering, power-loss behavior and I/O latency remain hardware validation work.

## Version 1 layout

The file is 263,168 bytes: two banks of 131,584 bytes. Each bank starts on a 512-byte boundary, with a 512-byte header followed by space for 65,535 little-endian 16-bit IDs. The final two padding bytes are outside the payload checksum.

Header fields are magic `ETOR` at byte 0; 16-bit version at 4; 16-bit entry count at 6; 32-bit identity fingerprint at 8; 32-bit generation at 12; 32-bit payload byte count at 16; 32-bit payload FNV-1a at 20; and 32-bit header FNV-1a at 24. The header hash covers bytes 0 through 23. Payload length is exactly twice the entry count; an empty payload hashes to the normal FNV-1a initial value. Unused header bytes are zero when written and reserved for future versions. These checks detect corruption; they are not authentication.

The active bank is never modified by a job. Its successor uses the other bank and increments the generation modulo 2^32. On the following edit, that older spare bank can be reused; this retains one previous committed copy rather than an unbounded history.

## Queue traversal

`play_queue_use_order` binds a validated library-ID order to a queue. It checks the library fingerprint and ID limit, captures the generation, and traverses the exact stored order with normal next/previous/repeat behavior. It does not apply a second shuffle or expand album groups: the stored sequence already represents the chosen order. The queue rejects its view after a new generation commits, until the caller rebinds it and sets the intended cursor. Building a normal generated queue clears the binding.

`PlayQueueStream` freezes the queue plan and cursor. Album-shuffle preparation visits at most 16 albums per step, then reads the flattened song sequence in bounded blocks. Its embedded cache is addressed through a self pointer: initialize it in its final location and do not copy or move an initialized stream. All source objects and library tables must remain stable for the job.

The player merges the base history, actual current song, pending manual songs and remaining base sequence into one stored queue. A song started from manual Up Next receives its own cursor; duplicate IDs remain distinct entries. Move and remove operations target future rows, including wrapped rows with Repeat All, and rebase the current cursor without restarting the song. The current track is verified against the pending data before publication.

Named mixtapes persist full path/cue identities and resolve them after rescans. Working queue IDs remain tied to their library association; use [saved mixtapes](mixtapes.md) for durable named lists.

## Verified bounds

With the device compiler's actual Cortex-M3 `-Os` flags, `TrackOrder` is 36 bytes and `TrackOrderJob` is 48 bytes. Data working buffers are 512 bytes each. The largest individual reported stack frame is 1,104 bytes; this is not a whole-call-chain stack bound and excludes driver frames. There is no heap or module-owned mutable global state.

Eighteen store behavior cases exercise edits, duplicates, limits, cancellation, staged power cuts, torn writes, corruption, uncertain readback, generation wrap and a 128-edit comparison with an array model. Nine FAT/queue cases exercise real FAT16/FAT32/exFAT file code, full-capacity orders, remounts, write failure, explicit traversal and stale generations. They run on the host; they do not establish physical card or player behavior.

## Player controls and persistence

In Up Next, hold Play on a future row to move it first, earlier or later, or remove it. Hold the current song for **Edit full queue**, which materializes the sequence without changing its order. Generated queues retain their 500-row preview until materialized; stored queues expose all remaining rows up to 65,535 entries. **Keep only current song** removes both history and future entries. Before materialization, **Clear added songs** retains its existing meaning and preserves the generated base list.

Adding songs or using Play Next on a stored queue inserts into that sequence, including duplicate entries. During a full edit, the app shows save progress and accepts Back to cancel. Power, output release and session saves cancel an unfinished edit before saving the old queue. The output pauses for the edit and resumes its previous state after completion or cancellation. Verification and publication yield between bounded data blocks; initial FAT reservation and reopening still perform synchronous I/O. Device latency and controller write ordering remain unmeasured.

An edited order stays fixed when the shuffle preference changes; that preference applies when starting the next library list. This preserves deliberate moves and removals. Repeat and next/previous continue to operate on the exact edited sequence.

The player lazily reserves `/.echo/order2.bin` at 263,168 bytes. Its authoritative root is `/.echo/session2.bin`, reserved at 4,096 bytes with two 512-byte journal records. Each record holds resume state, pending manual songs, queue kind and the exact order generation together. The legacy `resume.bin` and `upnext.bin` files are read-only migration inputs. Once a valid new journal exists, those older files are not kept in sync.

Before editing data, the player checkpoints the current session. It writes and verifies the spare data bank, then publishes a root record naming that generation. Only a verified root publication replaces the live queue. A failed or cancelled data edit retains the old root. A complete data bank with no published root is an orphan and is ignored on restart. Restore tries the newest valid root and its exact data generation, then the older root if its data cannot be validated. Unsupported record versions refuse writes; an uncertain root readback requires reload before another save. A complete root may exist after uncertain readback, so uncertainty does not prove that the edit was absent from disk.

The root record begins with magic `ESJN`, little-endian version 1 at byte 4, payload size 268 at byte 6, generation at byte 8 and FNV-1a at byte 12. Its hash covers bytes 0–11 followed by the payload at bytes 16–283. The payload is the existing 44-byte resume encoding, 216-byte manual queue encoding, a 32-bit stored-order generation, a one-byte queue kind and three reserved bytes. Queue kinds are generated (0), stored (1), or an empty base with a manually started song (2). Both kinds of generations use serial arithmetic and support wrap through zero. These checks detect corruption and torn writes; they provide no authenticity or physical durability guarantee.

The session journal's largest measured individual ARM `-Os` frame is 1,304 bytes (`session_journal_save`); `player_restore` uses 552 bytes and edit initialization uses 272 bytes. These are individual compiler-reported frames, not whole-stack bounds, and exclude called HAL/driver frames.
