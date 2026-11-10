# Listening log

FreshBase can keep a record of what was played: one entry per play in a ring, and totals that outlive the ring. `playback/listening_log` stores them, `playback/listening_stats` computes the statistics, `playback/listening_export` writes the plays as a scrobbler file and `playback/listening_track` derives a track's keys from the library. Block format, generations and publication are in `playback-store.md`.

Nothing in the app calls these modules yet, and none of this has run on a player.

## File

`/.freshbase/listens.bin` is 263,168 bytes. Byte order is little-endian.

| Offset | Bytes | Content |
|---|---|---|
| 0 | 512 | counters, copy 0 |
| 512 | 512 | counters, copy 1 |
| 1,024 | 4,096 × 64 | the ring: one 64-byte record per slot |

A file the player creates reads as zero in its first 4 KB and holds whatever was on the card after that. Both counter blocks are inside the cleared part, so a new file is an empty log whatever its ring holds.

## Counters

Each copy is one header block (magic `DSLL`, version 1, count 0, binding 0) with 92 bytes of payload inside it:

| Offset in the block | Type | Field |
|---|---|---|
| 28 | `u32` | sequence number of the next play |
| 32 | `u32` | records the ring holds, at most 4,096 |
| 36 | `u64` | time listened, in ms |
| 44 | `u32` | plays |
| 48 | `u32` | skips: plays that ended as skipped |
| 52 | 4 × `u64` | time listened per output, in ms |
| 84 | 3 × `u64` | time listened per format class, in ms |
| 108 | `u32` | boots |
| 112 | `u32` | charge sessions |
| 116 | `u32` | full charges |

The counters are the root of the ring: they say which records exist. `listening_log_open` takes the newer copy that is ready and whose payload decodes, and the other one when that fails.

| Result | State afterwards |
|---|---|
| ready | counters and ring as stored |
| empty | a new log: all counters zero, next sequence 1, no records |
| corrupt | both copies are damaged: the log starts again as a new one and the records in the ring are no longer read |
| unreadable, unsupported | nothing is written until the log is opened again, so a later version is never overwritten |

## Records

A play with sequence number `s` is in slot `s mod 4,096`. The first play has sequence 1.

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | sequence number |
| 4 | `u32` | track key |
| 8 | `u32` | album key, 0 for none |
| 12 | `u32` | artist key, 0 for none |
| 16 | `u32` | length of the track in ms, 0 when not known |
| 20 | `u32` | time listened in ms |
| 24 | `u32` | position where the play started, in ms |
| 28 | `u32` | position where it ended, in ms |
| 32 | `u32` | seconds since start-up when the play started |
| 36 | `u32` | clock when the play started, 0 when no clock was known |
| 40 | `u32` | the boots counter when the play was logged |
| 44 | `u16` | index of the track in the library at that time |
| 46 | `u8` | how it ended: 0 finished, 1 skipped, 2 stopped, 3 replaced by another track |
| 47 | `u8` | output: 0 3.5 mm, 1 4.4 mm, 2 Bluetooth, 3 USB DAC |
| 48 | `u8` | format class: 0 lossy, 1 lossless, 2 high resolution |
| 49 | 11 | 0 |
| 60 | `u32` | FNV-1a of bytes 0–59 |

- The keys identify a track, its album and its artist across rescans. The track key is the hash of the identity mixtapes use (`mix_identity_hash`: path, start and end, `mixtapes.md`). The album key is `library_album_key`, which favorites and covers use. The artist key is the FNV-1a hash of the track artist's name, cut at 255 bytes. `listening_track_read` reads all three for a track of the open library.
- The library index is a hint for finding the track again without a search. Whoever uses it compares the key.
- The clock is in seconds since 1970-01-01 00:00 in the player's local time. The caller supplies it; the layer never reads a clock.
- `listening_format_class` gives the class: lossy; lossless up to 48 kHz and 16 bits; high resolution for lossless above either.

With `next` and `kept` from the counters, the ring holds the sequences from `next − kept` to `next − 1`. A slot is read as a play only when its checksum holds, its three kinds are in range and it carries exactly the sequence its slot stands for. Any other slot in that range is skipped and counted as damaged; slots outside it are never read.

## Writing

`listening_log_append` logs one play with two writes:

1. the 64-byte record is written to its slot and read back. On a card that is one sector;
2. the counter copy not in use is written with the next generation, the new totals and the new `next` and `kept`, and read back (`store_publish`).

| Result | Meaning |
|---|---|
| done | the play is in the ring and in the totals |
| failed | nothing changed: the record did not read back, or the counters were not published. A record without counters that name it is not read, and the next play is written over it |
| uncertain | the counters may or may not have been published. Nothing more is written until the log is opened again |
| idle | the log is switched off |

`listening_log_note` counts a boot, a charge session or a full charge with step 2 alone.

`ListeningLog.session` adds up the plays logged since the log was opened. It is kept in memory only.

The off switch is `enabled` in the configuration and `listening_log_enable`. While it is off, `listening_log_append` and `listening_log_note` return idle, write nothing and count nothing. Statistics and the export still read what is there.

## Statistics

The statistics are jobs over the ring: `begin`, then `step` until the result is no longer working. A job copies `next` and `kept` when it begins, so a play logged while it runs is not part of its result. Each step is one read of at most 512 bytes, eight records. A pass over a full ring is 512 or 513 steps. A read that fails ends the job as failed.

Two terms: a play is every record. A listen is a play that ran for at least half the track's length, or that finished when the length is not known (`listening_play_listened`).

### Summary

`listening_summary_begin` takes the current time on the same clock as the records, or 0 when there is none.

| Field | Meaning | From |
|---|---|---|
| `all_time` | time listened, plays, skips | the counters; survives the ring wrapping |
| `session` | the same since the log was opened | memory |
| `today` | the same for plays that started on today's date | the ring |
| `week` | the same for plays from Monday of this week up to today | the ring |
| `streak_days` | days in a row with at least one play, ending today, or yesterday when today has none yet | the ring |

- Without a clock the job is done at once without reading: `clock_known` is false and `today`, `week` and `streak_days` are zero.
- A play without a clock, or dated after today, counts for no day.
- With a clock the job takes one pass. A streak of 64 days or more takes one more pass for every further 64 days.
- `listening_skip_permille` gives the skip rate of any of these totals: skips per thousand plays.

### Top lists

`listening_top_begin` ranks tracks, albums or artists by time listened. The result is at most ten entries, longest first; equal times rank by key. An entry has the key, the time listened in ms, the listens and one play of it (`track`: the keys and library index of its newest record) for the lookup below. `replayed` is the entry with the most listens; for tracks that is the most replayed track.

- An entry's time stops at 4,294,967,295 ms (49.7 days).
- A key of 0 is not ranked, and neither is an entry without any time listened.
- The ranking covers the plays the ring holds, not all time.

Adding up the time per key needs a table. The caller lends one: `ListeningTally` entries of 12 bytes. A pass adds up the keys with the lowest values that fit the table and are above the last key done, offers them to the ten, and the next pass continues above them. The result is the same for every table size; only the number of passes changes:

| | Reads |
|---|---|
| passes | distinct keys ÷ table entries, rounded up, at least 1 |
| each pass | one read per sector that holds records, 512 or 513 for a full ring |
| at the end | one 64-byte read per entry, at most 11 |

A full ring with 1,500 different tracks takes 47 passes with 32 entries (384 bytes) and 5 passes with 341 entries (4 KB).

## Export

`listening_export` writes the plays, oldest first, as an Audioscrobbler portable player log, the format of Rockbox's `.scrobbler.log`. `LISTENING_EXPORT_PATH` is `/.scrobbler.log`.

```
#AUDIOSCROBBLER/1.1
#TZ/UNKNOWN
#CLIENT/FreshBase 1.0.0
Björk	Homogenic	Hunter	1	254	L	1794182400	
```

A line is artist, album, title, track number, length in seconds, rating, the clock when the play started and the MusicBrainz recording id, separated by tabs and ended by a line feed.

- The rating is `L` for a listen and `S` for any other play.
- The time is the record's clock: local time, which `#TZ/UNKNOWN` states. A play logged without a clock has the time 0.
- The client line is the name and version of `identity.json`.
- The text comes from the caller through `ListeningLookup`: one call per play with the record's `ListeningTrack`, filling a `ListeningText`. A play the lookup does not know is left out and counted in `unknown`. A track number of 0, an empty album and an empty recording id are written as empty fields.
- A tag is at most 127 bytes and is cut at a whole UTF-8 character. A control character in a tag is written as a space.

The player's file writer cannot grow a file, so the job has two parts on one `ListeningExport`:

1. `listening_export_begin` and steps until done: one pass that adds up the size of the text in `bytes`. Nothing is written.
2. `listening_export_write` with a file, and steps until done: the text is written in blocks of 512 bytes, one play and at most one write per step.

A file larger than the text is filled to its end with comment lines: `#`, 62 spaces and a line feed, the last one shorter. A file smaller than the text gets the newest plays that fit; the oldest are left out and counted in `dropped`. A write that fails ends the job as failed.

No scrobbling tool has been run on a file written this way.

## Limits

- 4,096 plays in the ring (`LISTENING_LOG_CAPACITY`, a power of two up to 32,768). The time a top list takes grows with the capacity times the number of different keys in it.
- The ring is not bound to a library. A play of a track that is no longer in the library keeps its place in the statistics and is left out of the export.
- The keys are 32-bit hashes. Two tracks, albums or artists with the same key are counted as one.
- The checksums find damage and torn writes. A power cut while a record is written can also cost the other records of that sector on the card, up to seven older plays; they are then skipped as damaged.
- The sequence number starts again at 1 when both counter copies were damaged.
- One owner writes the file. A computer can change it during a USB session, so it is opened again after one.

## Tests

`test_listening_log` (the round trip, the layout, the two writes of a play, the counters, wrap-around, a write cut at every byte of the record and of the counters, damaged and misplaced records, a new file over old records, the off switch, the counter copies, an uncertain write, a later version), `test_listening_stats` (the summary with and without a clock, the week, streaks across 64 days, each top list on a hand-made log, the most replayed track, table sizes from 1 to 64 against a count made by the test, damaged records, a full ring, a play logged during a job), `test_listening_export` (the file byte for byte, no clock, control characters, the fill, a file that is too small, unknown tracks, long tags, failed writes) and `test_listening_track` (the keys against the library fixture and the CUE fixture).
