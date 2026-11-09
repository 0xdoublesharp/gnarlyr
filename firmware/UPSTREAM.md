# Where Doublesharp Echo comes from

Doublesharp Echo is built on two open-source firmwares, both GPL-3.0-or-later. [`CREDITS.md`](CREDITS.md) says who wrote what.

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

| Doublesharp commit | EchoPod commit | What |
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
