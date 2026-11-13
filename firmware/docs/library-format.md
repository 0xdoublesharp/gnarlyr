# Library database format (version 1)

The file formats Gnarlyr keeps in its data folder for the library, covers, favorites and bookmarks. The PC indexer (`tools/library_index.py`) writes the library database. The device reads it (`src/library`). Everything is sorted on the PC, so the device never sorts. It reads fixed-size records by index and strings by offset, a few hundred bytes at a time. The four-byte magics that start with `DS` are format tags named for the maker, Doublesharp, and remain stable across display-name and identifier changes.

- Location on the card: `/.gnarlyr/library.db`
- Byte order: little-endian
- Indices: `u16` (at most 65,535 tracks, albums, groups and folders)
- `0xFFFF` means "none"

## Header (128 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic `DSDB` |
| 4 | `u16` | version = 1 (the player rebuilds a library of any other version) |
| 6 | `u16` | section count = 10 |
| 8 | 10 × 12 B | section directory, in the order below |

Each section directory entry is `u32 offset`, `u32 count`, `u16 record size`, `u16 music count`. The music count is set for tracks, albums, artists, album artists, genres and title order (0 elsewhere); see Spoken word below.

| # | Section | Record size | Content |
|---|---|---|---|
| 0 | tracks | 32 | grouped by album in album order, then disc, track number and title |
| 1 | albums | 16 | sorted by title, then album artist |
| 2 | artists | 12 | track artists, sorted by name |
| 3 | album artists | 12 | sorted by name |
| 4 | genres | 12 | sorted by name |
| 5 | folders | 16 | depth-first; folder 0 is the music root (empty name); CUE sheets are folders too |
| 6 | title order | 2 | track indices sorted by title, then artist (All Songs) |
| 7 | playlists | 12 | M3U playlists, sorted by name |
| 8 | links | 2 | shared `u16` lists that groups, playlists and folders point into |
| 9 | strings | 1 | NUL-terminated UTF-8; offset 0 is the empty string; duplicates stored once |

## Optional ReplayGain extension

The device scanner and PC indexer append a checked gain table after the normal sections when gain metadata exists. The base header and records stay at version 1, and the gain table is excluded from the content fingerprint used by saved playback and labels. The extension layout, validation and reserved-file publication rules are in [ReplayGain metadata](replay-gain.md#library-cache).

## Records

| Record | Layout |
|---|---|
| **Track** | `u32 title`, `u32 path`, `u16 album`, `u16 artist`, `u16 genre`, `u16 folder`, `u32 duration_ms`, `u8 disc`, `u8 number`, `u16 reserved`, `u32 start_ms`, `u32 end_ms` |
| **Album** | `u32 title`, `u16 album_artist`, `u16 genre`, `u16 year`, `u16 first_track`, `u16 track_count`, `u16 cover` |
| **Group** (artist, album artist, genre, playlist) | `u32 name`, `u32 first_link`, `u16 link_count`, `u16 reserved` |
| **Folder** | `u32 name`, `u16 parent`, `u16 subfolder_count`, `u32 first_link`, `u16 track_count`, `u16 flags` (bit 0: a sheet, a CUE sheet or the chapters of a file; bit 1: the sheet is a chaptered `.m4b`, so the folder holding the file doesn't list it) |

Field notes:
- `u32` string fields hold an offset into the strings section.
- Track `path` is relative to the music root and uses `/`. Every string is Unicode NFC (composed). FAT cards store names in NFC, but macOS lists them decomposed (NFD), so the indexer recomposes paths, playlist names, playlist entries and tags. The device then matches paths against the card's names without any Unicode tables. Track `artist` indexes the artists section, and `genre` indexes genres.
- An album's tracks are `tracks[first_track .. first_track + track_count)`. Album `cover` is reserved and always `0xFFFF`. Covers live in their own file, keyed by album key (see Covers file).
- A group's albums are `links[first_link .. first_link + link_count)`. Artists list every album containing at least one of their tracks. Artist and album-artist albums are ordered by year, then title; genre albums are ordered by title. Within each section the groups' links are consecutive and in group order (group `n + 1` starts where group `n` ends). Cover Flow relies on this for album artists to read its order straight from `links`; a file that breaks it gets Cover Flow in album order instead.
- A playlist's links are track indices in playlist order. Its name is the file name without `.m3u` or `.m3u8`.
- A folder's items are `links[first_link ..]`: first `subfolder_count` folder indices (sorted by name), then `track_count` track indices (sorted by disc, number, file name). The tracks are one per audio file: a file split by a CUE sheet is there once, as the sheet's first track (see CUE tracks).

## Fingerprints

Track, album and folder indices mean something only for the database they were read from. Files that store indices carry a fingerprint of that database and are ignored when it differs.

- The layout fingerprint (`library_fingerprint`) is the FNV-1a hash of every section's offset and count, from the header alone. Two databases with the same counts and section sizes share it, for example after retagging a song with a title of the same length. The session journal and `favorite_added.bin` use it.
- The content fingerprint (`library/library_content`) is the FNV-1a hash of the 128-byte header followed by every section's records, in section order: every byte that decides what an index means, including a folder's flags and the music counts. Padding between sections and the unused rest of the reserved file are left out, so the same database has the same fingerprint wherever it is stored. The stored track order and the session journal use it (`docs/playback-store.md`).

The content fingerprint costs one pass over the database, so it is not part of `library_open`. `library_content_begin` and `library_content_step` compute it, at most 512 bytes and one read per step, and the result is kept in the `Library`: `library_content_fingerprint` reports it without reading, and reports nothing until the pass has finished, after a failed read, for a library that did not open, and again after `library_open` or a new pass. The in-memory extra playlist (`Library.extra`) is not part of it. `test_library_content` checks each of these.

## Chapters of an .m4b

Both the desktop indexer and device scanner read a `.m4b` file like `.m4a` (`library/tags_mp4`) and, when it has at least two usable chapters, becomes one track per chapter, the same way a CUE sheet does (`library/mp4_chapters` and `tools/mp4_chapters.py`):

- Chapters come from the Nero `chpl` atom in `moov/udta` first, then from the QuickTime chapter text track (`trak/tref/chap` names a text track; each sample is a 16-bit length and UTF-8 text, its start time from `stts` and its place from `stsc`, `stsz` and `stco` or `co64`). Entries that start no later than the previous one or at or past the file's duration are dropped, the first chapter starts at 0, at most 255 chapters are read (the last one runs to the end of the file) and a chapter title is cut at 63 bytes. A chapter without a title is "Chapter n". Chapter data that is missing, has fewer than two chapters or doesn't fit the file leaves one ordinary track.
- Each chapter track has the file's tags with `title` the chapter's, `number` the chapter's place (1 to 255), `start_ms` its start, `end_ms` the next chapter's start (0 for the last) and `duration_ms` its own length.
- The chapters are the tracks of a folder with flags 3 (a sheet whose chapters bit is set), named as the file without its extension, a child of the folder holding the file. Unlike a CUE sheet the folder holding the file doesn't list the file as a track: its `track_count` and links leave it out. Every chapter track still has the folder holding the file as its `folder`.
- The PC indexer (`tools/library_index.py`) lists an `.m4b` as one track and reads no chapters; the player's own rescan reads them.

## CUE tracks

A track with `end_ms` or `start_ms` other than 0 is one part of an audio file: it plays the file from `start_ms` to `end_ms`, and to the end of the file when `end_ms` is 0. Every other track has both at 0 and plays the whole file. `duration_ms` is the length of the part (`end_ms - start_ms`, or the file's length minus `start_ms` for the last part), so everything that shows or compares a track's length reads `duration_ms` alone. `path` is the audio file's path, the same for every track of the file; the file itself has no track of its own.

A `.cue` file makes tracks in the folder scan when it is usable (`library/cue_sheet`, `tools/cue_sheet.py`):

- The sheet has exactly one `FILE` line and at least one `TRACK` with an `INDEX 01 mm:ss:ff`. A sheet with several files (one per track), without a track or without an index is ignored, and the audio files list as usual. `INDEX 00` and other indexes are ignored; a `TRACK` without `INDEX 01` is left out.
- The audio file is the one in the sheet's folder named by the `FILE` line (the part after the last `/` or `\`, compared ignoring ASCII case), otherwise the audio file with the sheet's own name without its extension (ASCII case ignored; the first by file name when several). Only `.flac`, `.wav` and `.mp3` count. A sheet with no such file is ignored. A usable sheet's audio file is not a track; two sheets that name one file both make tracks.
- The sheet is read like a playlist: UTF-8 (with or without a BOM) when the whole file is valid UTF-8, otherwise Windows-1252; lines split like `str.splitlines`, trimmed, keywords case-insensitive (ASCII). A value is the text between the first pair of quotes (to the end of the line when the closing quote is missing; there are no escapes); unquoted, `FILE` takes the first word and `TITLE`, `PERFORMER` and `REM` values take the rest of the line.
- `INDEX 01 mm:ss:ff` is `(mm * 60 + ss) * 75 + ff` frames, `floor(frames * 1000 / 75)` milliseconds. A track starts at its `INDEX 01` and ends at the next kept track's; the last one has `end_ms` 0.
- Fields: title is the track's `TITLE`, else "Track N"; artist is the track's `PERFORMER`, else the sheet's `PERFORMER`, else the file's artist; album artist is the sheet's `PERFORMER`, else the file's (guessed per album as for any track without an album-artist tag); album is the sheet's `TITLE`, else the file's tags and fallbacks; genre is `REM GENRE`, else the file's; year is the first four digits of `REM DATE`, else the file's; number is the `TRACK` number (at most 255); disc and the other fallbacks come from the file as for a track. `REM` values count only before the first `TRACK`.
- Tracks come at the position of the `.cue` file in the sorted walk, in sheet order. The player cuts texts at 255 bytes; the PC indexer doesn't.
- A usable sheet is also a folder with flag bit 0 set: named as the `.cue` file, a child of the folder holding it (sorted by name among its subfolders), with no subfolders and with the sheet's tracks in sheet order. The folder holding the audio file lists the file once, as the sheet's first track; the sheet's other tracks are only in the sheet folder. Every track of the sheet still has the folder holding the file as its `folder`. A file named by two sheets is listed once per sheet. Folders shows every audio file by its full file name with extension and the sheet by its name, and opens the sheet's tracks by title; the file's row shows the whole file's length (the sheet's last track's `start_ms` plus its `duration_ms`), and selecting it plays the sheet folder from its first track.

## Spoken word

A track is spoken word when its first folder is `Audiobooks`, `Audiobook`, `Podcasts` or `Podcast`, or its filename ends in `.m4b` (ASCII case-insensitive; `library/spoken_path`, `spoken_of` in the indexer). An explicit Podcast folder takes precedence over the M4B extension. Spoken word is kept out of Music lists. The shelves merge matching named roots; Audiobooks also projects M4B books from other paths. A chaptered file contributes one chapter-folder entry, and a whole file contributes one track entry. The original folder tree, paths and record layout stay intact. Rescan an existing library or rebuild it with echo-sync to apply classification changes.

Every sort and grouping key of a spoken track starts with a spoken flag that sorts after music, so spoken word never shares an album, artist, album artist or genre with music: an author with music too appears twice, once among the music groups and once among the spoken ones. Each of those sections and the title order therefore hold their music entries first, then the spoken ones, and the header's music count says where the music ends. Tracks follow album order, so spoken tracks come last too. The Music lists (Cover Flow, Albums, Artists, Album Artists, Genres, Songs, shuffle) only read the first `music count` entries; folders and playlists show everything. Covers are built for every album, spoken ones included.

## Sorting

Sort keys are case-insensitive and accent-insensitive (Unicode NFKD with combining marks removed), and a leading "The " is ignored. So "The Beatles" sorts as "beatles" and "Björk" as "bjork".

The desktop chapter reader seeks past audio payloads and reads only bounded atom headers, table fields and chapter text. It supports standard and extended atom sizes, version 0/1 media clocks, fixed or variable sample sizes and 32/64-bit chunk tables within the player's 32-bit file-offset limit. Chapter parsing is separate from Mutagen metadata loading so a malformed chapter box can fall back to a valid referenced text track or a whole-book record without losing readable tags.

## Files and tags

The indexer lists every format stock plays, and Opus: `.flac`, `.ogg`, `.opus`, `.mp3`, `.m4a` and `.m4b` (AAC or ALAC), `.wav`, `.wma`, `.ape`, `.dsf` and `.dff`, skipping hidden files and folders (a name starting with `.`). Each field takes the first non-empty value, trimmed:

| Field | Vorbis comment (FLAC, Ogg Vorbis, OpusTags) | ID3 (MP3, WAV, DSF, DFF) | MP4 atom | APEv2 (any case) | ASF (WMA) |
|---|---|---|---|---|---|
| title | `title` | `TIT2` | `©nam` | `Title` | `Title` |
| artist | `artist` | `TPE1` | `©ART` | `Artist` | `Author` |
| album artist | `albumartist` | `TPE2` | `aART` | `Album Artist` | `WM/AlbumArtist` |
| album | `album` | `TALB` | `©alb` | `Album` | `WM/AlbumTitle` |
| genre | `genre` | `TCON` (ID3v1 numbers become names) | `©gen` | `Genre` | `WM/Genre` |
| date | `date` | `TDRC` (`TYER` in ID3v2.3) | `©day` | `Year` | `WM/Year` |
| disc | `discnumber` | `TPOS` | `disk` (first number) | `Disc` | `WM/PartOfSet` |
| track | `tracknumber` | `TRCK` | `trkn` (first number) | `Track` | `WM/TrackNumber` |

An MP3 without ID3v2 uses its ID3v1 tag. The duration is the container's length rounded to the nearest millisecond (FLAC and DSF sample counts, Ogg's last granule, less Opus pre-skip, an MP3's Xing/Info frame count or its bitrate, MP4's track duration, WAV data length, APE frame counts, ASF play duration minus preroll, DFF DSD data length). `tools/tag_fixture.py` writes one small file per format to `tests/fixtures/tags`, and both the Python indexer and the device reader (`src/library/track_tags`) are tested against them.

On the device, `track_tags_read` reads through a `ByteSource` with small bounded reads, keeps the first 255 bytes of each value (UTF-8, trimmed as Python's `strip()` does) and computes durations in double precision in the same order as mutagen, so the rounded milliseconds match. It also reports where the cover is: a front cover (picture type 3, or MP4's `covr`) wins over any other picture. For Ogg and Opus, the cover is the base64 `METADATA_BLOCK_PICTURE` text: its offset counts bytes of the Ogg stream from the start of the comment packet's first page body, page headers excluded, and the picture also records that page's file offset. A FLAC file's covers come from its PICTURE blocks only; `METADATA_BLOCK_PICTURE` comments in FLAC are ignored, as mutagen ignores them. ID3 tags with unsynchronisation are read through a view that drops the escape bytes: the whole tag for ID3v2.2 and v2.3, each flagged frame for v2.4; a picture found this way records the raw region so the cover pass can read it the same way. Known difference from the PC indexer: values aren't recomposed to NFC.

## Building on the device

`library/library_builder` builds a database that means the same as the PC indexer's, from tracks given in scan order (the PC indexer's sorted path order), with a block of RAM and a scratch file on the card (`/.gnarlyr/build.tmp`, 24 MB, reserved with the other player files). It handles up to 65,535 tracks; 20,000 are tested inside 360 KB of RAM. RAM only ever holds what one step needs: buffers, small caches and a few 16-bit columns.
- During the scan each track becomes a 60-byte row on the card (text offsets and lengths, folder, year, disc, number, duration, start, end, CUE sheet folder and whether it follows the sheet's first track) and its texts go to a heap on the card (a text equal to the previous track's same field is stored once). Folders become 8-byte rows (the top bit of the name length marks a CUE sheet); a hash of (parent, name) in RAM finds a folder seen before. The scanner hands each CUE track its sheet's file name, and the builder adds the sheet folder under the track's folder.
- Before ranking, tracks marked as having no album-artist tag (a spare row byte) get their shared album artist: one record per such track (folder, track, artist text, album and artist sort keys) is sorted by folder, album key, artist key and track; one pass picks each group's winner, a second writes (track, artist text) pairs, which are sorted by track and merged back into the rows in one sequential rewrite. Nothing runs when no track is marked.
- Each field is then ranked: one record per run of equal text (its sort key, its bytes and its first track) goes through a stable external merge sort on the card (sorted chunks in RAM, then up to 32-way merges), giving a dense key rank per track, a spelling rank for genres, and each group's first spelling. Folder names are ranked by key, then bytes.
- Every view is built from integer tuples with the same sort, in the PC's order: albums (grouped by album artist and album; first sight, earliest non-zero year, most common genre spelling with ties to the first seen; members by disc, number and title), album order (title, then album artist, then first sight), the final track order, artist and album-artist links by year and title, genre links by title, preorder folders (sorted rank paths) with their links, and All Songs by title then artist. Playlists resolve through RAM tables of 16-bit positions keyed by path hashes, confirmed against the paths rebuilt from the card.
- `library_builder_finish` writes the file in two passes over the card data: records with string offsets counted in emission order, then the strings (track titles and paths in scan order, then album, group, folder and playlist names), through a buffer of 512 bytes to 8 KB; the `DSDB` magic goes in last so an interrupted build reads as no library. Titles and paths are written per track and names per group, without the PC's global de-duplication, so the bytes differ from the PC's file while every record, link and string means the same. It reports running out of memory or scratch room rather than truncating.
- Tests decode both files and compare them field by field (`tests/library/library_compare.c`): the fixtures `small_library_tracks.db` and `builder_random/`, and seeded random libraries of 1 to 20,000 tracks (`tests/library/random_library.c`) against the previous in-RAM builder kept as a test-only reference (`tests/library/reference/`).

`library/library_scan` walks the card for it through a `ScanFileSystem` (list a folder, open a file): each folder is read whole, names starting with `.` are dropped, the rest sorted by their UTF-8 bytes (the PC's part-wise path order), and the walk goes depth first without recursion, with one folder open at a time. Files with a stock extension get `track_tags_read` and `library/track_fields` (the fallbacks below), then go to the builder. A file whose tags can't be read is skipped and counted. An eighth of the scratch block (16–96 KB) holds the folder listings unless a separate listing buffer is given; the builder gets the rest and the scan job's scratch file. Its test scans `tests/fixtures/tags` through the simulator's file HAL and must mean the same as `tests/fixtures/tags_library.db`, which `tags_library_up_to_date` re-indexes with the PC tool.

## Fallbacks for missing tags

| Missing | Used instead |
|---|---|
| title | file name without its number prefix |
| artist | album artist, then "Unknown Artist" |
| album artist | artist, then shared per album (below) |
| album | folder name |
| genre | "Unknown Genre" |
| disc and number | `<disc>-<track> - ` or `<track> - ` file-name prefixes, otherwise disc 1, track 0 |
| year | first four digits of `date`, otherwise 0 |

**Albums without an album-artist tag.** Tracks whose album artist came from the artist (no album-artist tag) and that share a folder and an album title (by sort key) are one album: they all get the album artist that most of them have as artist, ties going to the one whose sort key comes first, spelled as the earliest such track spells it. So an album with a featured artist on some tracks ("The Weeknd & Justice" among "The Weeknd") stays one album instead of splitting. Tracks with an album-artist tag keep it, and the same album split over folders (`CD1`, `CD2`) still joins when their artists agree. The PC indexer does this in `guess_album_artists` before building; the device builder does it as a pass over the track rows before ranking (below).

## Playlists

The indexer reads every `.m3u` and `.m3u8` file under the music root, skipping hidden folders. A file is decoded as UTF-8 (with or without a BOM), or as Windows-1252 if that fails. Blank lines and `#` lines are ignored, and so are URLs, except for the start directive: a line that is exactly `#GNARLYR-START:` and one to ten digits below 4294967295 (case-sensitive, after trimming) gives the start in milliseconds of the next entry only, the last directive before an entry winning. The player writes it for a track of a CUE album (a track whose start or end is not zero), whose path is the album's audio file.

Each entry is matched to a track, ignoring case and treating `\` as `/`:

1. relative to the playlist's folder
2. relative to the music root
3. otherwise by the longest matching run of trailing path parts, so absolute Windows paths such as MusicBee's `D:\Music\Artist\Album\01 - Song.flac` still match. A tie counts as not found.

An entry with a start then picks, among the tracks sharing the matched track's path (casefolded), the one with that start; the highest track index wins a repeat. No such track leaves the matched one, which for a path shared by the parts of a CUE album is the last one: a plain path to a CUE-split file means its last track. The suffix match (step 3) finds nothing for such a file, because its parts tie, directive or not.

Entries that match nothing are left out, and the indexer reports how many each playlist lost.

On the device, the scanner notes each `.m3u`/`.m3u8` path during the walk (the PC's order) and reads them after the tracks, once `library_builder_prepare` has fixed the track order and built the path indexes: an exact table keyed by the casefolded path (Python's `str.casefold`, from a second generated table; the last of equal keys wins, as in a Python dict) and chains by casefolded file name for the suffix match. `library/playlist_reader` checks the whole file for strict UTF-8 (a BOM is skipped) or decodes Windows-1252 with U+FFFD for its five undefined bytes, splits lines like `str.splitlines` (U+0085, U+2028 and U+2029 only in UTF-8) and trims them; each entry is resolved at once, so entries are never stored (an entry's `#GNARLYR-START` is read with it, and the part is picked by walking the chain of its file name for the track with that start and the same casefolded path). Candidates go through `posixpath.normpath` rules, including its two leading slashes. Playlists are sorted by sort key and then name, and their names share the string table with everything else. `scan_music/` (nested folders, a hidden folder, an AppleDouble file and three playlists in UTF-8 with a BOM, Windows-1252 and plain UTF-8) and `builder_random/with_playlists` (30 random playlists, 379 entries) check it byte for byte.

## Album keys and favorites

Favorites outlive a re-index, so they don't store album indices. An album's key is the FNV-1a hash of its album artist, a `0x1F` separator and its title (`library_album_key`). `/.gnarlyr/favorites.bin` holds the magic `DSFV`, `u16` version 1, a `u16` count, the `u32` keys and an FNV-1a checksum. At most 256 albums can be favorites. An album that leaves the library just stops showing up.

## Hidden tracks file

`/.gnarlyr/hidden.bin` (4,864 bytes, made by the player when a song is first hidden or deleted; `app/hidden_tracks`) keeps the songs the owner hid and the songs hidden or deleted since the last library build. Little-endian:

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | magic `DSHD` |
| 4 | `u32` | layout fingerprint (`library_fingerprint`) of the library the runtime set belongs to; 0 after a rescan |
| 8 | `u16` | hidden records in use (at most 16) |
| 10 | `u16` | 0 |
| 12 | `u16` | runtime set count (at most 12) |
| 14 | `u16[12]` | track indices |
| 38 | `u16[12]` | the folder that lists each track in a folder walk (its CUE or chapter folder when it has one) |
| 62 | `u16[12]` | the tracks' places in the title order, sorted |
| 512 | 16 × 272 B | hidden records: a `MixIdentity` (`u16` path length, `u32` start, `u32` end, path) per hidden song, in the order they were hidden |

The records are what the owner hid, by identity, so they survive a rescan: the rescan leaves those songs out of the new library (`app/hidden_scan`). The runtime set is what `library_browse` and the player skip in the library that is open; it is used only while its fingerprint matches the open library, and a successful rescan writes 0 there, because the new library already leaves out the hidden songs and the deleted files. A deleted file has no record.

## Bookmarks file

`/.gnarlyr/bookmarks.bin` (4 KB, reserved by the player) remembers where each audiobook and podcast episode stopped (`playback/bookmarks`). Like favorites, it is keyed by path hashes, so it outlives a re-index.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic `DSBK` |
| 4 | `u16` | version = 1 |
| 6 | `u16` | count (at most 128) |
| 8 | `u32` | reserved (0) |
| 12 | count × 16 B | bookmarks, most recent first |
| 12 + 16 × count | `u32` | FNV-1a checksum of everything before it |

Each bookmark is `u32 key`, `u32 track key`, `u32 position_ms`, `u16 chapter`, `u8 flags` (bit 0: finished), `u8 reserved`. The key is the FNV-1a hash of the book's folder path relative to the music root (`Audiobooks/The Long Road`), or of the track's own path for a single-file book, a podcast episode or a chapter of a chaptered `.m4b` (the chapters folder's key is the file's path hash); the track key hashes the track's path, with the track's `start_ms` added when it isn't 0, so every chapter of one file has its own. `chapter` is the track's place among its folder's tracks, checked against the track key before it's used. A zero-filled or damaged file reads as no bookmarks.

## Covers file

The player builds `/.gnarlyr/covers.bin` after every rescan (see `docs/architecture.md`, Covers on the device); `tools/cover_export.py` writes the same format on a PC. It's separate from the database, keyed by album key, so either file can be rebuilt on its own.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic `DSCV` |
| 4 | `u16` | version = 1 |
| 6 | `u16` | cover count |
| 8 | `u16` | full size = 112 |
| 10 | `u16` | half size = 56 |
| 12 | `u32` | stamp table offset, or 0 for none |
| 16 | count × 8 B | index: `u32 album key`, `u32 record offset`, sorted by key |

Each record is the full cover (112 × 112 pixels) followed by the half cover (56 × 56), as little-endian RGB565 rows, top to bottom. That's 31,360 bytes per album, so about 6.8 MB for 217 albums. Readers follow the record offsets, so records may start after a larger index: the player reserves index room for every album that fits the file before it knows how many have art.

The player also keeps a stamp per cover so a rescan can reuse it (see `docs/architecture.md`, Covers on the device). For a file of `capacity` bytes it has room for `slots = (capacity − 16) / (8 + 4 + 31,360)` covers: the index area holds `slots` entries, the stamp table follows at `16 + slots × 8` with a `u32` stamp for each index entry in the same order, and record `n` starts at `16 + slots × 12 + n × 31,360`. Readers that don't use stamps ignore the table. A stamp of 0 means unknown: the player decodes that album again. `docs/architecture.md` (Covers on the device) defines the stamp.

echo-sync (`encode_stamped`) writes this layout for the card's `covers.bin` size, with the stamps the player would compute: for embedded art from the track's size and the picture's length (the same bytes mutagen returns), for a cover file from its size and the card's modification stamp (`card_stamp`, as for the music fingerprint). It writes 0 for a cover file when the card isn't FAT or exFAT, since the time can't be matched. `tools/cover_export.py` on its own (`--output`) still writes the packed layout, with 0 at offset 12 and records right after the index; the player reads such a file normally and rebuilds every cover on its next rescan.

The exporter takes, for each album:
1. the embedded front cover (picture type 3) of the album's first track, or that track's first embedded picture of any type (FLAC PICTURE blocks, ID3 APIC, MP4 `covr`, APE and WMA pictures, Ogg and Opus `METADATA_BLOCK_PICTURE` comments)
2. when there is none, or it is not a picture the player decodes (a PNG, or a baseline, extended or progressive 8-bit JPEG with 1, 3 or 4 components) or Pillow can't open it, `cover`, `folder`, `front` or `albumart` (`.jpg`, `.jpeg` or `.png`, any case) in a folder holding its tracks

A JPEG's EXIF orientation is applied. The image is cropped to a centered square and resized with Lanczos filtering, separately for each size. Albums without art are left out and listed when the export finishes; the device draws an initials cover for them.
