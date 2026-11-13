# Gnarlyr

Gnarlyr is firmware for the full-size Snowsky (FiiO) Echo music player (480 × 222 screen). It replaces FiiO's firmware on the player. Snowsky and FiiO are trademarks of their owners, and Echo is their product name. Gnarlyr is not made, endorsed or supported by FiiO.

## Status

- **Version 1.0.0, not released.** There is no image to download.
- **Developer builds run on the owner’s full-size ECHO.** The canonical identifier transition is being prepared; current hardware checks are recorded in the known-issues document.
- An earlier build of this project, based on Podium 4.5.5, was installed on the owner's Echo (4 GB) through maskrom. The owner confirmed display, menus, buttons, music playback, volume, USB storage with internal storage and a microSD card, and quiet audio with the 12 ms DAC wait. That unit was also returned to FiiO's 2.0.0 through maskrom and back.
- [`docs/known-issues.md`](docs/known-issues.md) lists what is not proven on a player.

## Where it comes from

Gnarlyr builds on [Podium](https://podium-echo.online) 4.5.9, open-source firmware for this player by another author. Podium is a fork of [EchoPod](https://github.com/BasilSkyWalk/echopod), the firmware for the Echo Mini. The interface, the library, playback, the updater, the Echo port and the equalizer are their authors' work. [`UPSTREAM.md`](UPSTREAM.md) records the exact starting point and what has been ported since; [`CREDITS.md`](CREDITS.md) lists everyone whose work is used.

## What the code does

The source supports the capabilities below. The known-issues document distinguishes software coverage from checks completed on the player.

- **Interface:** Home, Music (Cover Flow, Playlists, Artists, Album Artists, Albums, Genres, Folders, All Songs, Favorites), Now Playing, Up Next and Settings. Three text sizes, eight highlight colours, landscape and two portrait orientations.
- **Playback:** FLAC, MP3, WAV and `.m4a`/`.m4b` with AAC-LC or Apple Lossless. Standard rates from 8 to 192 kHz; rates below 32 kHz are converted to 44.1 or 48 kHz. Gapless between native-rate tracks at the same sample rate. Shuffle by song or album, repeat, resume after power-off.
- **Library:** built on the player from the tags, with album covers from embedded art or a `cover`, `folder`, `front` or `albumart` JPEG or PNG. CUE sheets split a one-file album into tracks. `tools/echo_sync.py` builds the same library on a computer.
- **Lists:** `.m3u` and `.m3u8` playlists, a `Favorite Tracks.m3u` the player appends to, album favorites, and an Up Next queue.
- **Audiobooks and podcasts:** their own Home entries, a bookmark per book and episode, and chapters from `.m4b` files.
- **Equalizer:** FiiO's presets and a ten-band Custom curve in 0.5 dB steps, eight saved slots, and curves read from files in an `EQ` folder.
- **Controls:** a button lock, three ways to skip songs, and buttons on wired 3.5 mm headsets.
- **Bluetooth headphones:** pair, remember six, reconnect.
- **USB:** mass storage for internal storage and the microSD card, or a USB Audio Class 1.0 sound card (Settings → USB mode).
- **Install and updates:** the first install is a file copy: FiiO's updater installs `ECHOECHO.IMG` from the top level of internal storage. Gnarlyr's updater then installs `GNARLYR.IMG` or the same file as `ECHOECHO.IMG`, and FiiO's own image under FiiO's `ECHOVnnn.IMG` names, which is the way back to FiiO's firmware; it tells them apart by their contents. It refuses the images of Podium, EchoPod and any other brand.
- **Data:** the player keeps its files in a hidden `/.gnarlyr` folder (about 110 MB on the music storage). Deleting it rebuilds the library.

The name (Gnarlyr: the USB product name, the start-up splash and the Home title), the maker's name (Doublesharp: the USB manufacturer name), the identifier (`GNARLYR`: the image brand and the USB storage vendor), the version and the names of the data folder, the update file and the playlist directive live in one file, [`identity.json`](identity.json), which the build, the C code and the tools read.

## Documents

| Document | Contents |
|---|---|
| [`docs/user-guide.md`](docs/user-guide.md) | Using the player: music layout, buttons, settings, updating, going back to FiiO's firmware |
| [`docs/known-issues.md`](docs/known-issues.md) | Safety, what is unproven on a player, limits |
| [`docs/release-notes.md`](docs/release-notes.md) | What differs from Podium 4.5.9 |
| [`docs/device-install.md`](docs/device-install.md) | Installing by file copy, later updates, and maskrom for a player that won't start |
| [`docs/device-recovery.md`](docs/device-recovery.md) | Writing FiiO's firmware back through maskrom |
| [`docs/architecture.md`](docs/architecture.md) | Layers, rules, device platform, tests |
| [`docs/hardware.md`](docs/hardware.md) | The hardware the drivers talk to |
| [`docs/library-format.md`](docs/library-format.md) | Library, covers, favorites and bookmark file formats |
| [`ROADMAP.md`](ROADMAP.md) | Feature status |
| [`../AGENTS.md`](../AGENTS.md) | The repository and device rules |

## Build and test

Use the wrapper in the repository root. It checks the toolchain, keeps all build output on the Scratch volume and never touches a player. Prerequisites are in [`../README.md`](../README.md).

```sh
python3 tools/firmware.py setup
python3 tools/firmware.py build --preset device
python3 tools/firmware.py build --preset release
python3 tools/firmware.py test
python3 tools/firmware.py test --regex <test name>
python3 tools/firmware.py sim --card /path/to/music
```

| Preset | Output |
|---|---|
| `dev` | Host build of the simulator and the tests, under UndefinedBehaviorSanitizer |
| `device` | Cross-compiled `build/device/GNARLYR.IMG` with the developer rows in Settings → About |
| `release` | `build/release/GNARLYR.IMG` with the short About menu |

`firmware/build` is a link the wrapper makes to `/Volumes/Scratch/SnowskyEcho/build/<checkout name>`. Every image build prints the size report: used and free bytes against the code window, static RAM, the rescan overlay and the USB overlay.

`test` builds the `dev` preset and runs `ctest`: the unit tests, the simulator smoke tests, the Python tool tests, the `device` build, the emulator scenarios on its image, formatting, the no-comments check, generated files being current and the Cortex-M3 RAM audit.

## Simulator

The simulator runs the same app as the player. On the first start it creates the library, the covers and the save files in its system folder, scans the music folder and finds the covers. The music folder is only read.

| Device button | Keyboard |
|---|---|
| Volume up (moves menus down) | Down arrow |
| Volume down (moves menus up) | Up arrow |
| Play | Return or Space |
| Menu | M or Tab |
| Back | Backspace or Escape |
| Power | P |

| Option | Effect |
|---|---|
| `--scale N` | Window scale (default 3) |
| `--frames N` | Quit after N frames |
| `--headless` | No window |
| `--screenshot FILE.bmp` | Save the last frame on exit |
| `--card DIR` | Use DIR as the card root (read only) |
| `--system-dir DIR` | Read and write `/.gnarlyr/` (library, covers, resume, settings, favorites) in DIR |
| `--battery N` | Report N % battery (default 80) |
| `--charging` | Report the battery as charging |
| `--keys LIST` | Play a key script, for example `down,down,play,back:long` (`up`, `down`, `play`, `menu`, `back`, `power`, each optionally `:long`) |

Headless runs use a virtual clock, so scripted screenshots are deterministic. Without `--system-dir` the simulator can't save, and says so.

## Layout

| Path | Contents |
|---|---|
| `identity.json` | Name, maker, identifier, version, data folder, update file and playlist directive |
| `src/` | Portable app, freestanding C11, layered: `core` → `gfx`/`image`/`codec` → `fonts`/`ui`/`library`/`playback`/`settings` → `app`. `hal/` holds hardware interfaces only |
| `platform/sim` | SDL2 simulator: implements `hal`, owns `main()` |
| `platform/device/board` | Device-only drivers, start-up, linker scripts |
| `platform/device/logic` | Driver logic without hardware access, unit-tested on the host |
| `platform/device/core1` | Decoder programs for the second core |
| `tools/` | Python: image builder, size report, emulator, library indexer, `echo_sync.py`, font and fixture generators |
| `tests/` | One test executable per module, emulator scenarios, fixtures |
| `docs/` | The documents listed above |

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE). The bundled Inter font is under the SIL Open Font License 1.1 ([`assets/fonts/inter/OFL.txt`](assets/fonts/inter/OFL.txt)), and so are the glyph packs rendered from it. The simulator's decoders (dr_flac, dr_mp3, dr_wav) are public domain or MIT-0 ([`third_party/dr_libs/LICENSE`](third_party/dr_libs/LICENSE)). The player's MP3 decoder is libmad, GPL-2.0-or-later ([`third_party/libmad/COPYING`](third_party/libmad/COPYING)). The player's AAC decoder is FAAD2, GPL-2.0-or-later ([`third_party/faad2/COPYING`](third_party/faad2/COPYING)).
