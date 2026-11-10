# Mixtapes

A mixtape is a saved, named queue that outlives a rescan. `playback/mix_identity`, `mix_catalog`, `mix_tape` and `mix_resolve` store 32 of them with up to 65,535 entries each. Block format, generations and the writing steps are in `playback-store.md`.

## Identity of an entry

An entry is not a track index, since a rescan renumbers tracks. It is the track's path relative to the music root and its start and end in milliseconds (`MixIdentity`), so the parts of a CUE sheet or the chapters of an `.m4b` stay apart and an entry saved twice stays twice.

- The path is the library's, read whole (`mix_identity_read`): at most 258 bytes, which with the leading `/` and the terminator is the player's path limit. A longer or unterminated path is refused, never cut.
- A path is valid when no part between `/` is empty, `.` or `..`, and no byte is below 32, 127, `:` or `\`. An end of 0 means the end of the file; any other end is after the start.

## Catalogue

`/.freshbase/mixes.bin` is 6,144 bytes: two banks of 3,072, each a header block (magic `DSMC`, version 1, count 32, payload 2,560 bytes) and 32 entries of 80 bytes, one per slot:

| Offset | Type | Field |
|---|---|---|
| 0 | `char[64]` | name, UTF-8, terminated; no byte below 32 or 127, and not only spaces |
| 64 | `u32` | generation of the slot's tape |
| 68 | `u16` | entries on the tape |
| 70 | `u8` | which of the slot's two tape files holds it: 0 = `a`, 1 = `b` |
| 71 | `u8` | 1 = in use |
| 72 | `u32` | payload bytes of the tape |
| 76 | `u32` | 0 |

A slot that is not in use has an empty name, no entries, no bytes and tape file 0, and keeps its tape generation so the next tape of the slot gets a new one. For a slot in use the payload bytes are between 11 and 268 per entry.

- `mix_catalog_open` returns ready, empty, corrupt, unreadable or unsupported. Opening checks the payload checksum and every entry of the bank it chooses. A file the player creates reads as zero in its first 4 KB, which covers both header blocks, so a new file is an empty catalogue, and an empty catalogue reads as 32 unused slots. Corrupt means both banks are unusable: the catalogue then refuses edits rather than write over them.
- `mix_catalog_begin` changes one slot. It takes the generation the caller last saw and refuses another, so a menu drawn from an older catalogue cannot change a newer one. It checks the bank in use again, then the job writes the spare bank as a copy with that slot replaced: 12 steps.
- `mix_catalog_previous` steps back to the bank before, when it is older and valid.

## Tapes

Slot `n` has two files, `/.freshbase/mixNNa.bin` and `mixNNb.bin` (`mix_tape_path`). A tape is a header block (magic `DSMT`, version 1, count, generation, payload bytes, payload checksum) followed by its entries back to back:

| Offset | Type | Field |
|---|---|---|
| 0 | `u16` | path bytes, 1 to 258 |
| 2 | `u32` | start in ms |
| 6 | `u32` | end in ms, 0 for the end of the file |
| 10 | bytes | path, not terminated |

A tape file is reserved at 512 bytes plus its payload, so its size has to be known first:

1. `MixMeasure` reads the entries from a `MixSource`, 32 per step, and adds up their encoded sizes. An entry that is not valid fails the measure.
2. The app makes sure the slot's other tape file has that size.
3. `MixTapeWrite` writes that file with the steps of `playback-store.md`. The source must give the same entries again: fewer or more bytes than measured fail the job before the header is written.
4. A catalogue job publishes the slot with the new generation, tape file, count and bytes.

The tape a catalogue entry names is never written; a save goes to the slot's other file. A tape without a catalogue entry naming its generation is ignored.

`MixTapeCheck` opens a tape for the generation the catalogue names: the header block, then the payload once in 512-byte blocks, checking the checksum and that the bytes parse as exactly `count` valid entries. On failure `MixTape.load` says why (empty, corrupt, unsupported, unreadable). `mix_tape_next` then reads a checked tape entry by entry, one read each.

## Order of a save

The catalogue is the root of the tapes, so a save follows `playback-store.md` (Writing):

1. the catalogue is published with the slot as it is, so that neither bank names the tape file about to be written;
2. that file is measured for, sized and written (above);
3. the catalogue is published with the new entry.

Stopping before step 3 leaves the mixtape as it was. Renaming and deleting are catalogue jobs only; a deleted slot's tape stays on the card until the slot is saved again. `playback` provides the jobs; running them in this order, and sizing the files, is the caller's part.

## Loading

`MixResolve` turns a checked tape into track indices of the open library, to be stored with `mix_resolve_source` as a track order:

1. it clears a hash table in memory the caller lends: `u16` slots, the first power of two at or above twice the track count;
2. it enters every track of the library under the hash of its identity. A track whose path is not valid is left out. An identity that is in the library more than once is entered twice, and no further;
3. for each tape entry it follows the table and compares the whole identity, never the hash alone. Exactly one match gives that track. No match, or more than one, leaves the entry out and counts it as missing; more than one also counts as ambiguous.

The result keeps the tape's order and repeats. Nothing is written to the tape, so an entry that is missing today is found again when its file returns. Each step does at most 32 probes or identity reads.

The lent memory holds the table and one `u16` per tape entry: 393,214 bytes for 65,535 tracks and a full tape, which is the 393,216 bytes of the scratch memory the rescan borrows from core 1.

## Limits

- 32 slots, 63 name bytes, 65,535 entries, 258 path bytes. A full tape of the longest paths is 17,563,892 bytes.
- An entry follows its file by path: a renamed or moved file is missing until the mixtape is saved again.
- Two tape files per slot keep the tape in use and at most one before it.

## Tests

`test_mix_catalog` (restart, a failed write at each of the 7 writes, an unreadable publication, cancelling at every step, stale generations, a damaged newest bank, both banks damaged, all 32 slots, invalid names and sizes), `test_mix_tape` (paths, CUE boundaries and repeats; a full tape of the longest paths; damage; entries that do not parse under a matching checksum; an empty tape; a source that changes after measuring; one read per payload block; file names; invalid identities), `test_mix_resolve` (reordered tracks, missing entries, changed boundaries, ambiguous identities, and a full library and tape inside the scratch size).
