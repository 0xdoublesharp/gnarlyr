# Playback store

The files Doublesharp Echo keeps for an edited queue, the playback session and saved mixtapes share one block format and one way of writing it (`playback/store`). The formats built on it are in `stored-order.md`, `session-journal.md` and `mixtapes.md`.

| File | Kind | Magic | Version | Bound to the library | Payload |
|---|---|---|---|---|---|
| `/.doublesharp/order.bin` | stored track order, two banks | `DSTO` | 1 | yes | after the header block |
| `/.doublesharp/session.bin` | session journal, two records | `DSSJ` | 1 | yes | inside the header block |
| `/.doublesharp/mixes.bin` | mixtape catalogue, two banks | `DSMC` | 1 | no | after the header block |
| `/.doublesharp/mix00a.bin` … `mix31b.bin` | mixtape tapes, one per file | `DSMT` | 1 | no | after the header block |

The folder is the data folder of `identity.json`. Byte order is little-endian.

## Header block

Every bank, record and tape starts with a 512-byte block. Its first 28 bytes are the header; the rest is zero, except in the session journal, where the payload follows the header inside the same block.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic |
| 4 | `u16` | format version |
| 6 | `u16` | count (entries in the payload; 0 for the journal) |
| 8 | `u32` | generation |
| 12 | `u32` | payload bytes |
| 16 | `u32` | FNV-1a of the payload |
| 20 | `u32` | binding: the library's content fingerprint, or 0 |
| 24 | `u32` | FNV-1a of bytes 0–23 |

A block reads as one of five things (`StoreLoad`):

| Result | Meaning |
|---|---|
| empty | all 512 bytes are zero |
| ready | magic, header checksum and version match; for the journal the payload checksum too |
| unsupported | magic and header checksum match, the version does not. Nothing is written to that file until it is opened again, so a later format is never overwritten. A later version has to keep these 28 bytes |
| corrupt | anything else |
| unreadable | the read failed |

## Generations and banks

A generation is a serial number: `a` is newer than `b` when `a − b`, modulo 2³², is between 1 and 2³¹ − 1, so the count may wrap through zero. The first write of a file has generation 1.

A file with two banks keeps the published copy and the one before it. Opening reads both header blocks, takes the newer ready one, checks it in full, and falls back to the other only when that check fails (`store_pair_open`). The bank that is not chosen is the spare the next write uses, so its payload is not read. If either header block is unreadable or unsupported, the open reports that instead of choosing.

## Writing

A payload that follows its header block is written by `StoreWrite`, one step at a time, never to the published bank:

1. the spare's header block is zeroed;
2. the payload is written in blocks of at most 512 bytes;
3. the payload is read back in blocks of at most 512 bytes and its checksum compared with the one of the bytes written;
4. the header block is written, read back, and compared.

Each step is one write or one read. A job that is stopped, or fails, before step 4 leaves a spare without a valid header and the published bank as it was. The journal's records are one block, so they are written by step 4 alone.

A file that points at another one is its root: the journal names the generation of the stored order, a catalogue entry names a tape file and its generation. A root has two copies, and the older copy may still name the bank or file that the next job would overwrite. Whoever runs the jobs therefore publishes the root once more, unchanged, before such a job, so that both copies name what is in use; then writes and verifies the spare; then publishes the root that names it. Until that last step nothing that a restart reads has changed.

Step 4 decides the result by what the read-back shows, not by what the write call returned (`store_publish`):

| Read-back | Result |
|---|---|
| the header written (and, for the journal, a payload that matches it) | done |
| zeros, or a block that is not valid | failed: the previous copy is still the published one |
| a valid block that is not newer than the published one | failed |
| unreadable, another valid block, or an unsupported version | uncertain: the new copy may or may not be there. The owner refuses further writes until it is opened again, and opening shows which it is |

## File access

The caller hands over a `StoreFile`: a read callback, a write callback and the file's size. The callbacks address bytes inside one file that already has its full size, because the player's file writer cannot grow a file. No call asks for more than 512 bytes, and none reaches outside the size given. A write callback may fail after writing part of its bytes.

## Limits

- The checksums find damage and torn writes. They are not a defence against a file made to deceive.
- One owner writes a file at a time. Nothing here keeps a computer from changing the files during a USB session, so the files are opened again after one.
- The order of writes that reach the medium, and what a power cut during a write leaves, are those of the card and the FAT writer; `test_stored_queue` runs the stored order through the FAT16, FAT32 and exFAT code on card images, not on a player.

## Tests

`test_store` (header states, publication results, the write steps, bank choice), `test_store_roots` (a queue edit and a mixtape save in the order above, cut at every write: what a restart opens is whole each time, also from the older root copy; and what is lost when the extra publication is left out), and the tests of each format.
