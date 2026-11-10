# What Gnarlyr inherits and changes

Gnarlyr starts from the **Podium 4.5.9 source archive**, recorded unchanged as donor commit `1dee2bd`; `2e1788b` imports that tree. Podium descends from **EchoPod 0.6.0**. This comparison uses that pinned starting point, rather than claiming that a feature is unique against every later upstream release. [Ancestry and port records](../UPSTREAM.md) and [credits](../CREDITS.md) identify the donors and licenses.

## Inherited foundations

| Area | Already present in the starting firmware |
|---|---|
| Device and playback | Full-size Echo board support, display/buttons/storage, DAC and audio drivers, the updater, FLAC/MP3/WAV playback and same-rate gapless playback |
| Music library | Tag parsing, on-device indexing, album art, artist/album/genre/folder browsing, Cover Flow, favorites and M3U playlists |
| Audio and connections | Bluetooth, USB storage and USB DAC code, gain/output settings and the ten-band equalizer with saved curves |
| Formats and reading | AAC-LC/ALAC M4A playback, M4B audiobook chapters and CUE tracks |
| Interface | Three text sizes, landscape and portrait orientations, the button lock and wired-headset controls |

The common library/playback/interface foundation comes largely from EchoPod. Podium supplies the full-size Echo port and additions including portrait/larger text, the ten-band equalizer, M4A/M4B and CUE support, and its ALAC decoder. An inherited capability describes source provenance; it does not establish a completed hardware check on this player's unit. [Credits](../CREDITS.md), [user guide](user-guide.md) and [known issues](known-issues.md) separate those details.

Later EchoPod work was also ported deliberately: linker-map size reporting, link-time optimization, cold-code size optimization, overlay binding checks, and the USB/updater overlay with a shared loader. Those are credited ports, not Gnarlyr inventions; [UPSTREAM.md](../UPSTREAM.md#ported-from-echopod-since-the-starting-point) lists both donor and local commits.

## Gnarlyr additions

- **Identity and interface:** Gnarlyr by doublesharp, five coordinated themes, the shared icon treatment, cassette fallback artwork, and eight independent playback tap/hold mappings (`3523654`).
- **Queues and mixtapes:** editing the complete playback sequence, journaled queue/resume publication, named mixes with save/load/edit/rename/delete, and path-plus-CUE identities that resolve after a rescan. These extend the inherited queue and M3U features. [Stored orders](stored-order.md), [session journal](session-journal.md) and [mixtapes](mixtapes.md) describe the formats.
- **Listening and lyrics:** a listening log with totals/top lists/export, and timed sidecar lyrics (`3523654`; [listening log](listening-log.md)).
- **Development and capacity:** language packs, demand-loaded feature modules, the USB debug loop with reports/scripts/screenshots and automatic update/reconnect, plus frame pacing, idle sleeping and clock coasting. These extend the credited overlay foundation. [Architecture](architecture.md) describes the boundaries and measurements.

## Bugs corrected during this project

| Origin or affected path | Correction | Evidence |
|---|---|---|
| Podium DAC startup | Use a 12 ms output power-up wait instead of Podium 4.5.9's 10 ms, matching the observed FiiO sequence. | `3ce0504`; [upstream change record](../UPSTREAM.md#our-own-changes-since-the-starting-point), [DAC sequence](../platform/device/logic/dac_sequence.c) |
| Custom firmware USB handling | Make the USB supply pins follow cable power, correcting missed connections observed on the owner's Mac. | `99ec29c`; [hardware findings](hardware.md) |
| New queue/session integration | Identify database contents, not merely matching section counts/layout, before accepting stored track IDs; rebuild buffered next audio when edits replace the next song. | `3523654`; [identity tests](../tests/library/test_library_content.c), [audio-engine regressions](../tests/device/test_audio_engine.c) |
| New mixtape and control paths | Defer playback-screen navigation until asynchronous mix loading releases its scratch memory; let an active seek operation retain its cursor and confirmation keys despite custom mappings. | `3523654`; [mixtape tests](../tests/app/test_mixtape_ui.c), [control tests](../tests/app/test_configured_controls.c) |
| New lyrics path | Resolve a sidecar from the library's relative audio path using the file HAL's required root prefix. | `3523654`; [lyrics screen](../src/app/screens/lyrics_screen.c) |
| New redraw scheduling | Bound passive progress updates so short tracks do not keep the fast redraw rate active. | `57f05fd`; [frame pacing and device measurements](architecture.md#when-a-frame-is-drawn) |

**No bug in FiiO's closed-source stock firmware has been isolated and fixed by this project.** Gnarlyr replaces that firmware. The DAC correction fixes a Podium mismatch to stock; it is not a stock-firmware patch. Quiet listening was reported afterward, but the timing change was not established as the sole cause of the earlier hiss. Likewise, features that address stock limitations may be inherited from Podium/EchoPod rather than newly written here.

## Installed build and ongoing discovery work

The installed developer build is **`57f05fd`**. Its recorded checks include installation/reconnect, the new interface, saved-mixtape persistence/load, queue opening and a zero-underrun playback window. Cold-cycle resume, both-output listening and other remaining hardware checks are listed in [known issues](known-issues.md).

Current developer source adds metadata search, configurable sorting, combined filters, personal labels, track information and exact result-order playback. It also refines native Listen and USB/charging layouts, navigation checkpoints, live theme/EQ previews and staged Custom EQ editing. These changes are **not part of the installed `57f05fd` image**; their own device validation remains required.
