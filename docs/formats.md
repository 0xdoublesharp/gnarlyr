# Gnarlyr file and pack formats

Gnarlyr 1.0.0 is not released, so every format that Gnarlyr writes starts at version 1. A file whose magic, version, size or checksum does not match is refused: the player uses defaults, rebuilds it (the library) or skips it (a pack). Nothing reads an earlier development layout. Integers are little-endian. FNV-1a means the 32-bit hash with offset basis 2166136261 and prime 16777619. The detailed design notes for each format are linked below. This page is the reference that tools and the desktop app are checked against.

## Settings: `/.gnarlyr/settings.bin` (internal storage)

The file is 78 bytes, inside a 4 KB reserved file. Only the copy on internal storage is read.

| Bytes | Field | Valid values |
|---|---|---|
| 0–3 | magic `DSST` | |
| 4 | version | 1 |
| 5 | flags: bits 0–1 ReplayGain (Off, Track, Album, Auto), bit 2 prevent clipping, bit 3 battery percent, bits 4–5 plug-in answer (Ask, Storage, Charge, DAC) | bits 6–7 zero |
| 6 | brightness | 1–10 |
| 7 | screen timeout option | 0–6 |
| 8 | gain | 0–1 |
| 9 | output | 0–2 |
| 10 | start screen | 0–6 |
| 11 | shuffle | 0–2 |
| 12 | repeat | 0–2 |
| 13 | volume | 0–60 |
| 14 | auto-off option | 0–3 |
| 15 | USB mode | 0–1 |
| 16 | DAC volume | 0–60 |
| 17 | accent | 0–7 |
| 18 | equalizer preset | 0–8 |
| 19–23 | language code, ASCII letters, digits or `-`, zero-padded; all zero means the image default | |
| 24 | text size | 0–2 |
| 25 | skip gesture | 0–2 |
| 26 | orientation | 0–2 |
| 27 | theme | 0–4 |
| 28–37 | ten equalizer gains, half-decibels + 20 | 0–40 |
| 38–53 | extension block E0–E15 (below) | |
| 54–57 | FNV-1a of bytes 0–53 | |
| 58–73 | second block F0–F15 (below) | |
| 74–77 | FNV-1a of bytes 58–73 | |

The extension block:

| Byte | Field |
|---|---|
| E0 | bit 0 rewind on resume, bit 1 safe start volume, bit 2 24-hour clock, bits 3+ track transition (0–2) |
| E1 | safe start level, 0–60 |
| E2 | transition length in tenths, 5–60 |
| E3 | playback speed as a signed step from normal, −2 to +6 |
| E4–E8 | Home order, one nibble per item, low nibble first: a permutation of 0..L−1 with L ≥ 9, the rest zero |
| E9–E10 | Home hidden mask; only stored items other than Settings (item 3) may be hidden |
| E11–E12 | UTC offset in minutes, signed, −720 to 840 |
| E13 | status bar items; bit 4 power-on behaviour, bit 5 cassette style, bit 6 skip silence, bit 7 speech boost |
| E14 | bits 0–2 DAC filter (0–4), bits 3–4 battery care (0–2), bits 5–6 crossfeed, bit 7 Now Playing style high bit |
| E15 | reserved, zero |

The second block: F0 bit 0 compact status bar, bit 1 standby, bits 2–3 standby power-off option (0–2), bit 4 start and stop animation off, bits 5–7 zero; F1 long-press action in bits 0–2, bits 3–7 zero; F2 Bluetooth volume, 0–60; F3–F15 zero.

A file is refused when it is shorter than 78 bytes, has another magic or version, fails either checksum or has any value outside these ranges. Design notes: [architecture.md](../firmware/docs/architecture.md), Settings.

## Language packs: `/.gnarlyr/lang/<code>.pack` and the image

| Bytes | Field |
|---|---|
| 0–3 | magic `DSLP` |
| 4–5 | version, 1 |
| 6–7 | string count; must equal the firmware's catalogue |
| 8–11 | file size, at most 65,535 |
| 12–15 | layout hash: FNV-1a over each string ID, a zero byte and its module number, in segment order |
| 16–19 | checksum: FNV-1a of bytes 20 to the end of the index, continuing from FNV-1a of bytes 0–15 |
| 20–27 | language code, 1–5 of letters, digits or `-`, zero-padded |
| 28–55 | native name, UTF-8, at most 27 bytes, zero-padded |
| 56–57 | segment count; must equal the firmware's |
| 58–59 | zero |
| 60… | one 16-byte row per segment: module (u16, the first is 0, the resident text), string count (u16), offset (u32), size (u32), FNV-1a of the segment (u32) |

Segments follow the index in order and end at the end of the file. A segment is a table of u16 offsets, one per string, followed by its NUL-terminated UTF-8 strings. The resident segment, aligned to 4 bytes, plus the largest module segment must fit 18,432 bytes. Every placeholder `{n}` of the English text must be kept. The image stores its built-in packs behind a `DSLT` table: a u32 count, then a u32 offset and a u32 length per pack. Design notes: [language-packs.md](language-packs.md) and [architecture.md](../firmware/docs/architecture.md#languages).

## Theme packs: `.gnarlyr/themes/*.gtheme`

Magic `DSTP`, version 1, a 512-byte header and an IEEE CRC-32 at byte 12. The selection file `/.gnarlyr/theme.bin` is a pair of `DSTS` version 1 store records. Full layout: [theme-format.md](theme-format.md).

## Store records

These files use the two-bank 512-byte block of [playback-store.md](../firmware/docs/playback-store.md): magic, a u16 version (1 for all of them), generation, payload size and hash, and a header checksum. Another version is unsupported, and the player keeps it from being overwritten until it is reset.

| File | Magic |
|---|---|
| `session.bin` | `DSSJ` |
| `order.bin` | `DSTO` |
| `listens.bin` | `DSLL` |
| `mixes.bin` and the saved mixtapes | `DSMC`, `DSMT` |
| `moments.bin` | `DSMO` |
| `track_keys.bin` | `DSTK` |
| `recap.bin` | `DSRC` |
| personal tags | `DSTG` |
| `headphones.bin` | `DSHF`, carrying a 148-byte `DSHP` version 1 profile payload |
| `parametric.bin` | `PEQF`, carrying an 841-byte payload: bytes 1–3 zero, nine 84-byte filter sets, then nine 9-byte sound-shaping records |
| `theme.bin` | `DSTS` |

## Other Gnarlyr files

| File | Magic, version | Notes |
|---|---|---|
| `library.db` | `DSDB`, 1 | rebuilt by a rescan when refused; [library-format.md](../firmware/docs/library-format.md) |
| `covers.bin` | `DSCV`, 1 | |
| library gain table | `DSRG`, 1 | appended to `library.db`; [replay-gain.md](../firmware/docs/replay-gain.md) |
| `favorites.bin` | `DSFV`, 1 | |
| `buttons.bin` | `DSBT`, 1 | 32 bytes, FNV-1a at 28 |
| `bluetooth.bin` | `BT`, 1 | 234 bytes: version at 2, enabled at 3 (0–1), count at 4 (0–6), six 38-byte devices from 5, output at 233 (0–1) |
| `time.bin` | `DSTM`, 1 | |
| `trial.bin` | `DSUT`, 1 | |
| font packs | 1 | `tools/glyph_pack.py` |
| discovery preferences | 1 | |
| debug screenshots | 1 | `tools/echo_debug.py` |
| `scan.bin` | `DSSC` | the music fingerprint and the firmware tag that made it; no version |

`resume.bin` and `upnext.bin` from earlier builds are no longer read or created; the session journal holds that state.

## Desktop app

The desktop app (separate repository) reads `settings.bin` and language packs by this page. Any change to a layout here changes its version only after release; until then the layout is edited in place and both sides change together.
