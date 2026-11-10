# Stored track order

`playback/track_order` keeps a sequence of track indices on the card: a queue the listener has edited, or a mixtape that was loaded. It holds up to 65,535 entries, repeats allowed, and none of them in RAM. Block format, generations and the writing steps are in `playback-store.md`.

## File

`/.freshbase/order.bin` is 263,168 bytes: two banks of 131,584.

| Offset in a bank | Content |
|---|---|
| 0 | header block, magic `DSTO`, version 1: count, generation, payload bytes = 2 × count, payload checksum, binding = the library's content fingerprint |
| 512 | count × `u16` track indices |

The file must have its full size before it is opened. Only the first 4 KB of a file the player creates read as zero, so the second bank of a new file holds whatever the card had there, which counts as no order.

## Opening

`track_order_open` returns ready, empty, unreadable or unsupported.

- A bank counts only when its binding is the fingerprint given, its payload is 2 × count bytes, every index is below the track limit given and the payload checksum matches. A file of another library is empty.
- Empty means no usable order, not a list that was cleared: a cleared list is a ready order of 0 entries.
- Unreadable and unsupported set `needs_reload`; no job starts on such an owner.
- `track_order_open_at` takes only the bank of one generation. The session journal names the generation it published, so a newer bank that was written but never published is not picked up.
- Only the bank that is chosen has its payload read: for a full order that is 256 reads of at most 512 bytes.

`track_order_read` copies entries from the open bank in reads of at most 512 bytes and checks each index against the track limit again. The payload checksum is checked when the file is opened and before a new bank is published, not on each read.

## Editing

A `TrackOrderJob` writes the spare bank from a source (`TrackOrderSource`: read `count` entries from `first`) and one edit:

| Edit | Result |
|---|---|
| copy | the source as it is |
| move `from` → `to` | the entry at `from` ends up at `to` |
| remove `from` | one entry fewer |
| insert `track` at `from` | one entry more; refused at 65,535 entries |
| clear | no entries |

- The source is usually the order itself (`track_order_source`); a generated queue (`play_queue_stream_source`) or a resolved mixtape (`mix_resolve_source`) makes a new order. Entries that follow each other in the source are read in one call.
- `track_order_job_begin` checks the edit's bounds and the inserted index before anything is written. One job runs per owner at a time.
- `track_order_job_step` does one read or write of at most 512 bytes and returns working, done, failed or uncertain. Done switches the owner to the new bank and generation. Failed and `track_order_job_abort` leave the owner and the published bank as they were. Uncertain sets `needs_reload`.
- A full order takes 514 steps: 1 to clear the spare's header, 256 writes, 256 reads, 1 to publish.
- `track_order_job_progress` is 0 to 100.

A job only ever writes the bank its owner does not have open. An owner opened with `track_order_open_at` at the generation the journal names therefore never overwrites the queue a restart would restore; `playback-store.md` (Writing) has the order of the steps around a job.

## Playing a stored order

`play_queue_use_order` binds a `PlayQueue` to an open order: positions are the stored entries in order, with the usual next, previous, repeat and place. It refuses an order whose fingerprint is not the library's content fingerprint (`docs/library-format.md`), whose track limit is not the library's track count, or a library whose content fingerprint has not been computed. The queue remembers the generation it was bound to; after the order moves to a new generation every query fails until the queue is bound again. `play_queue_build` drops the binding.

`PlayQueueStream` is the opposite direction: a frozen copy of any queue read as a flat sequence, with the position of the current track in it. An album-shuffled queue is counted first, 16 albums per `play_queue_stream_prepare` call, and then read album by album; a read that goes backwards starts again from the first album.

## Limits

- 65,535 entries; indices are valid for one library content fingerprint only.
- The owner, its file callbacks, the source and the library must stay as they are while a job runs. The owner may be copied between jobs.
- Opening checks one bank synchronously.

## Tests

`test_track_order`: edits against an array model across block boundaries, repeats, bounds, cancelling at every step, a failed write at each of the three kinds of write with seven tear lengths, a payload that reads back differently, an unreadable publication, a damaged newest bank, another library, an unsupported version, generation wrap, a full order with transfers of at most 512 bytes, and reading only the chosen bank. `test_stored_queue`: the same file through the FAT16, FAT32 and exFAT code on card images: full orders after a remount, a failed card write, queue traversal, stale generations, and a shuffled queue stored and played back. `test_play_queue`: the stream against the queue it copies.
