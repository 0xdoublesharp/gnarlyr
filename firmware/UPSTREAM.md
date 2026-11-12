# Where Gnarlyr comes from

Gnarlyr is built on two open-source firmwares, both GPL-3.0-or-later. [`CREDITS.md`](CREDITS.md) says who wrote what.

| Project | What it is | Source |
|---|---|---|
| Podium | Firmware for the full-size Echo, forked from EchoPod 0.6.0 | <https://podium-echo.online> (releases after 4.5.5 are published there as source archives); <https://github.com/swap/podium> stops at 4.5.5 |
| EchoPod | Firmware for the Echo Mini, by BasilSkyWalk | <https://github.com/BasilSkyWalk/echopod> |

## Starting point

`firmware/` restarted from the Podium 4.5.9 source archive:

- Archive: <https://podium-echo.online/download/4.5.9/podium-4.5.9-src.zip>
- SHA-256: `c5af7301057072327ee468962cd161e0fd0d01a2b3df7dbcc40d6fb48713b0cd`
- In git: the archive's contents are recorded unchanged as `refs/donors/podium-4.5.9` (commit `1dee2bd`). Commit `2e1788b` made `firmware/` equal to that tree, and has it as a parent.

Git keeps the ancestry: `refs/donors/podium-4.5.9` sits on Podium's history, which descends from EchoPod 0.6.0 (tag `echopod/v0.6.0`). Two remotes are configured in the repository:

| Remote | URL | Notes |
|---|---|---|
| `echopod` | <https://github.com/BasilSkyWalk/echopod> | Its tags are fetched under `echopod/` (`echopod/v0.1.0` to `echopod/v0.11.1`) |
| `podium` | <https://github.com/swap/podium> | Ends at 4.5.5; later versions exist only as archives |

## Ported from EchoPod since the starting point

Each was written for the Echo Mini after EchoPod 0.6.0 and brought across by cherry-pick or by hand.

| This project's commit | EchoPod commit | What |
|---|---|---|
| `915126f` | `153de67` | Size report from the linker map |
| `d766e31` | `9995f0e` | Size report: libgcc member names from Windows paths on any host |
| `e6274b9` | `48ee7fd` | Link-time optimisation for the core 0 firmware |
| `b2c2c94` | `302e177` | `-Os` on more cold files |
| `f52fe9d` | `a8b1c93` | The rescan overlay is refused when it was linked against other code |
| `53873ba` | `788fd0b` | USB sessions and the updater run from a second on-demand overlay, with one shared loader. Ported by hand, with the loader in its EchoPod 0.11.1 shape |

`53873ba` differs from EchoPod's version: the overlay window is `0x18000` bytes with two linker regions over it, a size above the window is refused before any arithmetic on it, a failed load zeroes the whole window, the start-up `MASKROM.REQ` check moved to `board/update_request.c`, there are no language packs or resident tables, and the emulator reads the USB overlay's ELF too.

## Our own changes since the starting point

| Commit | What |
|---|---|
| `af9d9c1` | Fixture tools take FFmpeg from `PATH`; the emulator scenarios are separate checks |
| `d511d21` | The size report reads its limits from the linker maps (EchoPod's carried the Echo Mini's limits as constants) |
| `6b7e607` | `CORE_SEAM` keeps the functions the emulator replaces or watches out of link-time optimisation; the emulator fails when a named function is missing |
| `3ce0504` | The DAC output power-up wait is 12 ms, as in FiiO's firmware (Podium 4.5.9 waits 10 ms) |
| `ceb0f47` | The size report covers the USB overlay and fails when a library keeps variables in both programs |
| `cfb2037` | Emulator scenarios for the USB overlay loader |

The identity change (name, identifier, version, update file, data folder, file magics) follows these. [`docs/release-notes.md`](docs/release-notes.md) describes the result for users.

## Compared with the starting point

This comparison uses the pinned Podium 4.5.9 starting point above rather than every later upstream release.

### What the starting point already had

| Area | Already present in the starting firmware |
|---|---|
| Device and playback | Full-size Echo board support, display/buttons/storage, DAC and audio drivers, the updater, FLAC/MP3/WAV playback and same-rate gapless playback |
| Music library | Tag parsing, on-device indexing, album art, artist/album/genre/folder browsing, Cover Flow, favorites and M3U playlists |
| Audio and connections | Bluetooth, USB storage and USB DAC code, gain/output settings and the ten-band equalizer with saved curves |
| Formats and reading | AAC-LC/ALAC M4A playback, M4B audiobook chapters and CUE tracks |
| Interface | Three text sizes, landscape and portrait orientations, the button lock and wired-headset controls |

The common library/playback/interface foundation comes largely from EchoPod. Podium supplies the full-size Echo port and additions including portrait/larger text, the ten-band equalizer, M4A/M4B and CUE support, and its ALAC decoder. An inherited capability describes source provenance; it does not establish a completed hardware check on this player's unit. [Credits](CREDITS.md), [user guide](docs/user-guide.md) and [known issues](docs/known-issues.md) separate those details.

Later EchoPod work was also ported deliberately: linker-map size reporting, link-time optimization, cold-code size optimization, overlay binding checks, and the USB/updater overlay with a shared loader. Those are credited ports, not Gnarlyr inventions; [the port table](#ported-from-echopod-since-the-starting-point) lists both donor and local commits.

### What Gnarlyr adds

- **Identity and interface:** Gnarlyr by doublesharp, five coordinated themes, the shared icon treatment, cassette fallback artwork, and eight independent playback tap/hold mappings (`3523654`).
- **Queues and mixtapes:** editing the complete playback sequence, journaled queue/resume publication, named mixes with save/load/edit/rename/delete, and path-plus-CUE identities that resolve after a rescan. These extend the inherited queue and M3U features. [Stored orders](docs/stored-order.md), [session journal](docs/session-journal.md) and [mixtapes](docs/mixtapes.md) describe the formats.
- **Listening and lyrics:** a listening log with totals/top lists/export, and timed sidecar lyrics (`3523654`; [listening log](docs/listening-log.md)).
- **Development and capacity:** language packs, demand-loaded feature modules, the USB debug loop with reports/scripts/screenshots and automatic update/reconnect, plus frame pacing, idle sleeping and clock coasting. These extend the credited overlay foundation. [Architecture](docs/architecture.md) describes the boundaries and measurements.

### Bugs corrected during this project

| Origin or affected path | Correction | Evidence |
|---|---|---|
| Podium DAC startup | Use a 12 ms output power-up wait instead of Podium 4.5.9's 10 ms, matching the observed FiiO sequence. | `3ce0504`; [change table](#our-own-changes-since-the-starting-point), [DAC sequence](platform/device/logic/dac_sequence.c) |
| Custom firmware USB handling | Make the USB supply pins follow cable power, correcting missed connections observed on the owner's Mac. | `99ec29c`; [hardware findings](docs/hardware.md) |
| New queue/session integration | Identify database contents, not merely matching section counts/layout, before accepting stored track IDs; rebuild buffered next audio when edits replace the next song. | `3523654`; [identity tests](tests/library/test_library_content.c), [audio-engine regressions](tests/device/test_audio_engine.c) |
| New mixtape and control paths | Defer playback-screen navigation until asynchronous mix loading releases its scratch memory; let an active seek operation retain its cursor and confirmation keys despite custom mappings. | `3523654`; [mixtape tests](tests/app/test_mixtape_ui.c), [control tests](tests/app/test_configured_controls.c) |
| New lyrics path | Resolve a sidecar from the library's relative audio path using the file HAL's required root prefix. | `3523654`; [lyrics screen](src/app/screens/lyrics_screen.c) |
| New redraw scheduling | Bound passive progress updates so short tracks do not keep the fast redraw rate active. | `57f05fd`; [frame pacing and device measurements](docs/architecture.md#when-a-frame-is-drawn) |

**No bug in FiiO's closed-source stock firmware has been isolated and fixed by this project.** Gnarlyr replaces that firmware. The DAC correction fixes a Podium mismatch to stock; it is not a stock-firmware patch. Quiet listening was reported afterward, but the timing change was not established as the sole cause of the earlier hiss. Likewise, features that address stock limitations may be inherited from Podium/EchoPod rather than newly written here.

## Looking at upstream changes

EchoPod:

```sh
git fetch echopod --tags
git log --oneline echopod/v0.11.1..echopod/main
git log --oneline echopod/v0.6.0..echopod/v0.11.1 -- platform/device
git show <commit>
```

EchoPod's tree has the firmware at its root, so its paths lack the `firmware/` prefix used here. Its later commits rarely cherry-pick cleanly: EchoPod 0.11.1 and Podium 4.5.9 conflict in 161 files, and EchoPod targets another board.

Podium:

1. Check <https://podium-echo.online> and its [changelog](https://podium-echo.online/changelog.html) for a version after 4.5.9.
2. Download the source archive into a new, empty folder on the Scratch volume and record its SHA-256.
3. Extract the starting point next to it (`git archive refs/donors/podium-4.5.9 | tar -x -C <folder>`) and run `diff -ru` between the two folders.
4. Port what is wanted by hand, one change per commit, and add a row to the tables above.

What we changed against the starting point:

```sh
git diff refs/donors/podium-4.5.9 HEAD:firmware
git log --oneline af9d9c1..HEAD -- firmware
```

A ported piece follows the porting rules in [`CLAUDE.md`](CLAUDE.md): one implementation per problem, with its tests, inside the size budget.
