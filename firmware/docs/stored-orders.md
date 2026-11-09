# Persistent track orders

`playback/track_order` provides the shared sequence store for edited queues and playlists. It supports up to 65,535 entries, including duplicate IDs, without retaining the sequence in RAM. This is a storage and traversal foundation. The player does not yet create or edit these files through its menus, and the existing resume format does not yet bind a stored order.

## Ownership and API

The caller supplies a `TrackOrderConfig` with bounded read/write callbacks, the reserved file size, the identity-table fingerprint and its exclusive upper ID limit. The callbacks address one file, not a raw disk. A read or write must complete synchronously and report failure honestly; a failed write may have changed a prefix. Reads and writes never request more than 512 bytes. The application must reserve the full file before opening it because the current FAT writer cannot grow files.

Keep one `TrackOrder` owner per file and one job per owner. Callbacks, input sequence and identity table must remain alive and stable throughout a job. Do not reinitialize an owner while its job is running. The busy flag excludes another job using the same owner; it does not coordinate independent objects, processes or USB mass-storage access.

`track_order_open` validates both banks, headers, IDs and complete payload checksums. It distinguishes no valid committed order from an I/O failure. No valid copy is not evidence that a named playlist was intentionally cleared; an intentionally empty list is a ready record with zero entries. A library/table mismatch rejects the stored IDs. A checksum-valid header with an unsupported version refuses editing rather than being overwritten as an empty list; future versions must retain the protected header prefix or use a different file path. A valid older bank is selected when the newer bank is corrupt; serial-number comparison handles generation wraparound. A bank that cannot be read produces an unreadable result rather than inviting an overwrite of unknown state.

`track_order_job_begin` starts copy, move, remove, insert or clear. It validates bounds before writing. Repeated `track_order_job_step` calls invalidate the inactive header, write at most 256 IDs, verify at most 256 IDs' bytes, and finally publish the new header. A source adapter can supply generated queue entries; input is read in contiguous runs where possible. The event loop must yield between steps and hold a stable source. Cancellation before publication preserves the previous committed bank.

The commit writes its header last and reads it back. A write callback that reports failure after completing a valid header can still produce a successful commit, because readback establishes the actual stored result. If header readback itself fails, the result is uncertain: the in-memory owner retains its old bank, sets `needs_reload`, and refuses another edit until reopened. The caller must not describe that result as definitely unsaved. A zero-initialized job is idle and safe to step or cancel; terminal jobs do not perform more I/O.

`track_order_read` reads bounded blocks from the validated active bank and checks ID bounds. Whole-payload checksum validation occurs on open and before publication, not on every individual row lookup. The two-bank protocol is tested against interrupted and torn writes; actual controller ordering, power-loss behavior and I/O latency remain hardware validation work.

## Version 1 layout

The file is 263,168 bytes: two banks of 131,584 bytes. Each bank starts on a 512-byte boundary, with a 512-byte header followed by space for 65,535 little-endian 16-bit IDs. The final two padding bytes are outside the payload checksum.

Header fields are magic `ETOR` at byte 0; 16-bit version at 4; 16-bit entry count at 6; 32-bit identity fingerprint at 8; 32-bit generation at 12; 32-bit payload byte count at 16; 32-bit payload FNV-1a at 20; and 32-bit header FNV-1a at 24. The header hash covers bytes 0 through 23. Payload length is exactly twice the entry count; an empty payload hashes to the normal FNV-1a initial value. Unused header bytes are zero when written and reserved for future versions. These checks detect corruption; they are not authentication.

The active bank is never modified by a job. Its successor uses the other bank and increments the generation modulo 2^32. On the following edit, that older spare bank can be reused; this retains one previous committed copy rather than an unbounded history.

## Queue traversal

`play_queue_use_order` binds a validated library-ID order to a queue. It checks the library fingerprint and ID limit, captures the generation, and traverses the exact stored order with normal next/previous/repeat behavior. It does not apply a second shuffle or expand album groups: the stored sequence already represents the chosen order. The queue rejects its view after a new generation commits, until the caller rebinds it and sets the intended cursor. Building a normal generated queue clears the binding.

Player integration still needs incremental generation of album/song-shuffle snapshots, edit actions on generated rows, cursor preservation, resume association and explicit shuffle-change behavior. The current manual Up Next UI is unchanged. Saved playlists also need a catalogue and stable path/track identity mapping across library rescans; a library-row cache must never become the sole persistent identity of a saved playlist.

## Verified bounds

With the device compiler's actual Cortex-M3 `-Os` flags, `TrackOrder` is 36 bytes and `TrackOrderJob` is 48 bytes. Data working buffers are 512 bytes each. The largest individual reported stack frame is 1,104 bytes; this is not a whole-call-chain stack bound and excludes driver frames. There is no heap or module-owned mutable global state.

Eighteen store behavior cases exercise edits, duplicates, limits, cancellation, staged power cuts, torn writes, corruption, uncertain readback, generation wrap and a 128-edit comparison with an array model. Nine FAT/queue cases exercise real FAT16/FAT32/exFAT file code, full-capacity orders, remounts, write failure, explicit traversal and stale generations. They run on the host; they do not establish physical card or player behavior.
