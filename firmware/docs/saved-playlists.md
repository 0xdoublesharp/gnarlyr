# Saved playlists: implementation direction

This is the next feature after the resident storage module. The catalog, playlist files and menu controls described here are not implemented yet. Existing M3U imports and the full queue editor already work in source.

## User flow

A Mixtapes menu should let the owner create a named list, save the current queue, play or edit a saved list, replace it with the current queue, rename it and delete it. Loading a saved list produces an independent queue; changes become saved changes only through an explicit save/replace action. Queue editing keeps the current song and position using the existing implementation. A saved empty list is valid. Playback position belongs to the session rather than changing the saved list's order.

Keep imported M3U playlists available. A dedicated saved-list format may retain cue positions that ordinary M3U import does not distinguish. Cassette styling should present these as mixtapes, with normal menus and album art. Creation may provide a suggested name, but rename needs actual text entry rather than a fixed selection of labels.

## Track identity and rescans

Persist the full relative audio path plus cue start and end milliseconds. Preserve duplicate entries, including multiple occurrences of one cue track. Library row IDs and string offsets are caches and must not become the saved identity. A library rescan can reorder IDs while paths and cue boundaries remain stable.

Read paths without truncation: `library_read_string` deliberately truncates UI labels and is unsuitable for durable identity. Reject an identity that does not fit the supported playback path or lacks a terminator. Resolve hash matches by comparing the complete path and both cue boundaries; a hash alone must not select a song. A missing or ambiguous identity stays in saved data and is reported on load. Loading available songs must not silently rewrite the saved playlist or discard unavailable entries.

A bounded hash index can map current-library identities to IDs while loading. Core 1 provides 384 KB of scratch only after audio is released. Up to 131,072 uint16 slots plus 65,535 resolved IDs fit in 393,214 bytes; caller-owned job state and bounded I/O buffers live outside that arena. Resolve incrementally and release scratch before restarting a decoder. Building an edited queue from the resolved IDs reuses the existing TrackOrder job and exact-generation session publication.

## Persistence and allocation

Use a bounded catalog, initially 32 named slots, read in blocks rather than retaining every name in RAM. Two catalog records publish each slot's active data file, generation, entry count and name together. Catalog rename/delete operations should not rewrite the song payload. Stale action menus must check catalog revisions before making changes.

Each slot has two data files. Compute the candidate's encoded size first, then reserve only the space needed. The file payload contains bounded variable-length paths and cue positions, with count, size and checksums in its header. Do not reserve the worst-case path capacity for every song or preallocate every empty slot. If an inactive file is too small, remove and recreate that inactive file through the existing FAT allocation/deletion logic; the HAL still needs a matching remove operation on device, simulator and test implementations.

Checkpoint the current catalog before changing candidate data, verify the candidate completely, and publish the catalog reference last. Preserve the active file while writing the spare. Cancellation and ordinary write failures retain the previous published list. Unknown versions and uncertain readback refuse further writes until reloaded. Retain files referenced by either valid catalog record; reclaim an older version only after neither record references it. Reusing a deleted slot must not expose its previous contents after an interrupted creation.

## Placement and checks

Keep the resident module within 12 KB and the original main-code load budget unchanged. Codec/catalog algorithms and their UI callbacks can reside there; mutable state remains caller-owned. The resident entry ABI must change if its layout changes. Use short functions and bounded 512-byte I/O, with progress and cancellation for long operations.

Required behavior checks include exact order and duplicates, cue identity, reordered-library IDs, missing songs, untruncated UTF-8 paths, empty lists, rename/delete/reuse, growth of a spare file, full-capacity sequences, cancellation and torn writes. Exercise the menu through the compiled ARM image and real emulated FAT sectors, then restart and reload the saved list. Physical storage latency, durability and playback remain part of the device-validation work.
