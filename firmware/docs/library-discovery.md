# Search, sorting and personal tags

Music → Search opens the search editor. Submit an empty search to explore all songs. In a music list, hold Menu to open view options for that list. An opened album, artist, album artist, genre, folder or playlist keeps its scope; Favorites keeps the favorite albums. A folder scope includes its subfolders. Open a playlist before searching it.

Results show the query or scope, a track count, artist context and an indicator when filters are active. Search, sorting and filters operate on the same result view, with up to the library's 65,535 tracks.

## Controls

| Place | Control | Action |
|---|---|---|
| Results | Volume keys | Move through songs; hold to scroll |
| Results | Play | Play from the selected result |
| Results | Menu | Open view options |
| Results | Hold Play | Open Track info, My tags, Play Next and Add to Up Next |
| Text or number editor | Volume keys | Choose a character |
| Editor | Play / Menu | Add a character / erase the last character |
| Editor | Hold Play | Submit the search or save the value |
| Editor | Back | Discard the edit |
| Filters | Back | Apply the filters and return to results |
| Any discovery screen | Hold Back | Return Home |

List traversal follows the selected orientation. Search and tag editors use a character wheel with lowercase letters, digits, space, hyphen and underscore; numeric editors offer digits. Search text holds up to 63 UTF-8 bytes. The wheel itself enters ASCII characters.

Back cancels a running search or pending queue preparation. Another Back leaves the cancelled results. A cancelled search, no matches and a read/write failure have different messages. Menu reopens the options from an empty result.

## Query and order

Search matches a substring in title, artist, album, album artist, genre or file path, using Unicode case and accent folding. It treats a leading “The” as searchable text.

Sort by Title, Artist, Album, Year or Duration, ascending or descending. Original order preserves the source order, including an opened playlist's sequence and duplicate entries. Equal sort keys retain their source order.

Filters combine with AND: one artist, album and genre, inclusive minimum/maximum year, inclusive minimum/maximum duration in whole minutes, and any number of personal tags. A song must have every selected personal tag. Year comes from the album; duration is the individual track's length, including CUE tracks. Blank or zero removes a numeric bound.

Clear search removes only the text. Reset view clears text, filters and sorting while keeping the current scope. Hold Play on an artist, album artist, genre or playlist prepares that group's songs and plays from the beginning.

## Information and personal tags

Track info shows title, artist, album, album artist, genre, year, disc/track number, duration and path. My tags opens assignments for that song. The existing song actions in ordinary browse lists also offer Track info and My tags.

Music → My tags manages labels. New tag creates one; selecting a label opens Rename and Delete. On a song's tag list, Play toggles its assignment and hold Play opens Rename/Delete. Deletion asks for confirmation and removes that label's assignments.

There are up to 32 labels, each with a nonblank name of at most 32 UTF-8 bytes, and up to 65,535 tagged track identities. Personal tags stay in the music volume's hidden metadata directory; audio-file tags are not rewritten. Assignments use the full path and CUE boundaries so a rescan can change library row numbers without attaching labels to another song. Moving or renaming the audio file changes that identity.

## Saved views and playback

An applied view saves its text, sort direction and filters on internal storage. Text, sorting and numeric bounds can survive a changed library. Artist, album and genre selections are retained only for the same library fingerprint. Saved personal-tag filters clear when the label catalogue changes. The scope comes from the list you open. “View not saved” leaves the current view usable when preferences cannot be written.

Playing a result copies the exact ordered result IDs into the stored queue and selects the corresponding position only after publication succeeds. CUE identity, duplicates and the chosen order are preserved. Cancelling preparation or a failed publication keeps the previous queue. Play Next and Add to Up Next use the existing queue operations.
