# Smart lists

Music → Smart Lists offers five lists built from the library, the listening log (`listening-log.md`) and the favorites: Recently Added, Recently Played, Most Played, Never Played and Old Favorites. `playback/track_keys` keeps the table that ties song identities to the library, `playback/smart_list` computes the lists, and `app/screens/smart_list_screen` shows them. The code runs in the listening module.

None of this has run on a player yet.

## Where the lists live

One Music entry, Smart Lists, between Favorites and My tags, opens a menu with the five lists. A single entry keeps the Music menu short (twelve rows), groups the lists that share the same wait, empty state and cache, and keeps them in one module. Home is not changed.

## Rules

The player has no calendar clock, so "recent" and "a long while" are measured in plays and library updates, never in dates. A play is any record in the listening log; a listen is a play that ran for at least half the song, or finished when its length is unknown (`listening_play_listened`). A song is a music track of the library (spoken word is left out, as in every Music list). Songs are matched to plays by their identity key: the path, start and end that mixtapes use (`mix_identity_hash`), so the parts of a CUE file are separate songs and a rescan does not change a song's key.

| List | Songs in it | Order |
|---|---|---|
| Recently Added | songs that arrived after the first update of the track keys table | newest arrival first, then library order (album order) |
| Recently Played | songs with at least one play in the log | newest play first |
| Most Played | songs with at least one listen in the log | most listens first, then the newest play |
| Never Played | songs with no play in the log | newest arrival first, then library order |
| Old Favorites | favorite songs with no play among the last 500 plays | the longest since their last play first; favorites with no play in the log after them, in favorites order |

- Every list has at most 100 songs and lists a song once. A song in the library twice (the same file and range under two CUE sheets) appears once, as the first of its indices.
- Plays of songs no longer in the library are left out and do not take a place among the 100.
- The log holds the last 4,096 plays, so "in the log" means within those. A song last played before them counts as never played.
- Arrivals: the track keys table records, for each song, the update at which it first appeared. The table is updated whenever a smart list opens and the library is no longer the one it was built for (the library's content fingerprint changed). Songs present at the first update are the starting point (arrival 0) and are not "added". Each later update that finds new songs gives them the next arrival number; songs that left the library are dropped from the table, so a song that comes back counts as new again. Several rescans between two visits to the lists count as one update.
- Old Favorites: the favorite songs are the tracks of the Favorite Tracks playlist (in its order) followed by the tracks of the favorite albums (albums in favorites order, tracks in album order). A favorite counts as played recently when one of its plays is among the last 500 plays of the log, so with fewer than 500 plays logged only favorites never played can be old. 500 plays is about 30 hours of music at three and a half minutes a song.

## Track keys file

`/.gnarlyr/track_keys.bin` is 1,573,888 bytes, reserved with the other player files. Byte order is little-endian.

| Offset | Bytes | Content |
|---|---|---|
| 0 | 524,800 | bank 0 |
| 524,800 | 524,800 | bank 1 |
| 1,049,600 | 524,288 | scratch for the sorted runs of an update |

A bank is a header block (`playback-store.md`: magic `DSTK`, version 1) followed by its payload. The header's binding is the library's content fingerprint, its payload bytes are eight times the entries, and its count is the latest arrival number. The payload is one 8-byte entry per music track, sorted by key, then index:

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | identity key |
| 4 | `u16` | track index in the library the bank is bound to |
| 6 | `u16` | arrival: 0 for the starting point, then 1, 2, … (it stops at 65,535) |

A track whose identity cannot be formed (an empty path) has no entry. Opening takes the newer ready bank (`store_pair_open`); the payload checksum is checked when a bank is written, not at every open. The table is current when a bank is ready and its binding equals the open library's content fingerprint.

### Updating

`track_keys_update_begin` and steps until done, with a lent work buffer of `TrackKey` entries (1,024 in the listening module, 8 KB):

1. Keys: each music track's key is computed (a track record and its path, a few tracks per step). When the buffer is full it is heap-sorted and written to the scratch area as one sorted run. A library needs at most 64 runs, so the buffer must hold at least a 64th of the music tracks and at least as many entries as there are runs.
2. Merge: the runs are merged in key order, each run read through an equal share of the buffer, while the published bank is read once in key order alongside to carry each song's arrival over. The merged entries are written to the spare bank by `StoreWrite` (zero the spare header, write, read back and compare, publish the header), so a step that fails, or an update that is left, keeps the published bank.

The cost is one pass over the library's track records and paths, the runs written and read once (eight bytes per track each way) and both banks read or written once. Progress is the share of tracks keyed, then of the bank written.

## Computing a list

`smart_list_begin` and steps until done. A list that uses the log ranks in passes over the ring with a lent tally table (682 entries of 12 bytes in the listening module, sharing the 8 KB buffer): a pass tallies plays, listens and the newest play for the lowest keys above the previous pass that fit the table, then walks the track keys table over the same key range alongside, so each song is matched to its tally without a search. Recently Added reads only the table. Each step is one read of at most 512 bytes. Passes are the number of different songs in the log divided by the table size, rounded up, and each reads the ring once (512 or 513 reads for a full ring); the table is read once in total. The 100 picks are kept sorted by the list's order, so each song is offered once and a song that cannot get in is turned away by one comparison.

## Screen

- Opening a list first brings the track keys table up to date ("Checking the library... n%"), then computes the list ("Finding songs... n%"). Each frame runs steps for up to 12 ms and keeps the audio fed. Back or Home leaves and stops the job; nothing half done is kept.
- A computed list is kept (resident, 100 indices per list) until the listening log publishes again (a play, a boot or a charge counted), the track keys table changes or the favorites change. Opening it again then shows it at once; coming back to it from Now Playing or the song actions also keeps the selection.
- Rows show the title and the artist, like Search results; in portrait the artist is on a second line.
- Play plays the list from the selected song: the exact ordered track indices are copied into the stored queue the way Search does (`app/order_publish`, shared with Discovery), then Now Playing opens. Shuffle follows the Shuffle setting, as for every list. With a list of at most 100 songs the copy is a few small writes and runs at once. Play on the song already playing opens Now Playing without restarting it, as in the other lists. A failed copy keeps the previous queue and shows "Couldn't save queue".
- Hold Play opens the song actions of the other lists: Play Next, Add to Up Next, Add to Favorite Playlist, Track info and My tags.
- Each list has its own empty state; a missing or unreadable track keys file shows "History unavailable".

The listening, lyrics and smart list screens share one work area in the listening module (`screens/listening_work`); the screen on top claims it. The smart lists keep only the computed indices outside it, so after the module was evicted or another screen used the area, a list that was being computed starts again and a shown list refills its rows.

## Limits

- 100 songs per list (`SMART_LIST_CAPACITY`) and the 500-play window (`SMART_LIST_FORGOTTEN_WINDOW`).
- Arrivals are counted per update of the table, not per scan: rescans that happen while no smart list is opened merge into one arrival.
- Keys are 32-bit hashes; two songs with the same key count as one.
- Time on a player with a large library is not measured yet: the first update after a library change reads every track's path, and each list reads the ring once per pass.

## Tests

`test_track_keys` (the first update as the starting point, reopening, another library, arrivals across three libraries, removal and return, many small runs against one large run, a work buffer too small, failed writes at every step and an abandoned update keeping the published bank, CUE parts, progress, a short or missing file, a later version, a failed read), `test_smart_list` (each list's order, skipped plays, the cap of 100, listens against skips, arrivals, the empty log, Old Favorites ordering and the 500-play boundary, songs no longer in the library, a song listed twice, tally tables of 1, 7, 64 and 512 entries giving the same lists, progress, failed reads, refused jobs), `test_smart_list_screen` (rows with title and artist, each empty state, the library check and its wait, the cache and its invalidation by a play and by a favorite, Old Favorites, Recently Added after a library change, CUE parts, playing the exact order into the queue, Play on the loaded song, a failed queue write, the song actions, leaving while working, a missing file, the menu, all six sizes and orientations) and `test_app_modules` (Music → Smart Lists loads the listening module, plays from Never Played and comes back).
