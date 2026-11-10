# Session journal

`playback/session_journal` stores what is needed to continue after a restart as one record: the resume state, the songs queued by hand and which stored order the queue plays. They are published together, so a restart never sees the position of one queue with the entries of another. Block format, generations and publication are in `playback-store.md`.

## File

`/.gnarlyr/session.bin` holds two 512-byte records, at offset 0 and 512. A file the player creates reads as zero in its first 4 KB, so a new file is two empty records. A record is one header block (magic `DSSJ`, version 1, count 0, binding = the library's content fingerprint) with 268 bytes of payload inside it:

| Offset in the record | Bytes | Content |
|---|---|---|
| 28 | 44 | resume state, as `playback/resume_state` encodes it: queue plan, cursor, track, position, repeat, volume |
| 72 | 216 | songs queued by hand, as `playback/up_next` encodes them: up to 100 track indices |
| 288 | 4 | generation of the stored order the queue plays |
| 292 | 1 | queue kind |
| 293 | 3 | 0 |

| Queue kind | Queue | Requires |
|---|---|---|
| 0 generated | built from the plan in the resume state | order generation 0 |
| 1 stored | the stored order of that generation (`stored-order.md`); the cursor is a position in it | no songs queued by hand, cursor on a whole entry, the playing song not marked as started from Up Next |
| 2 empty | none: the playing song was started from Up Next with nothing else queued | the song marked as started from Up Next, order generation 0 |

## Reading

`session_journal_open` returns ready with the newest record that passes every check, and its state:

- the block is ready and its binding is the fingerprint given; a record of another library counts as empty;
- the payload is 268 bytes, both embedded encodings decode and carry the same fingerprint;
- the track and every queued index are below the track limit given, and the queue kind's requirements hold.

If the newest record fails, the other one is used. With no record to use the result is corrupt when one was damaged, otherwise empty; unreadable and unsupported set `needs_reload`. The caller's state is written only on ready.

`session_journal_previous` steps back to the record before the one in use, when it is older and passes the same checks. It is for a newest record whose stored order cannot be opened.

## Writing

`session_journal_save` writes the record that is not in use with the next generation, reads it back and returns done, failed or uncertain (`playback-store.md`, Writing). It is one 512-byte write and one 512-byte read.

- A state that would not pass the checks above is refused before anything is written.
- The record in use is never written, so a torn or refused write leaves the session that was there.
- Uncertain sets `needs_reload`: the next save is refused until the journal is opened again.
- A journal with two damaged records is written from generation 1 again.

## Publishing an edited queue

The journal is what makes a new bank of the stored order the queue, in the order of `playback-store.md` (Writing): the session is saved once more, so that neither record names the bank about to be written; the order job writes and verifies that bank; then a record naming its generation is saved. Until that save is done the queue in use stays the old one, and `track_order_open_at` ignores a bank no record names.

## Limits

- Two records: the session in use and the one before.
- 100 songs queued by hand; a stored queue has none, they are part of the order.
- The state is valid for one library content fingerprint.

## Tests

`test_session_journal`: the round trip, a write cut after every byte count from 0 to 512, an unreadable read-back, a refused write, the previous record and a damaged newest one, another library's record, invalid indices, an unsupported version, generation wrap, and the record's layout.
