# FreshBase release notes

FreshBase starts from Podium 4.5.9 ([`../UPSTREAM.md`](../UPSTREAM.md)). For everything up to and including that version, see [Podium's changelog](https://podium-echo.online/changelog.html).

## Current source candidate

- Search metadata and paths, combine sorting and filters, inspect track information and assign personal mood/activity labels. Scoped Play All and selected-result playback preserve the displayed order.
- Navigation returns to existing screen checkpoints. Mixtape actions settle at List, modal Back returns to its parent, and hold Back cancels work and reaches Home. Slides use gentle ease-in/ease-out.
- Native Listen and USB/charging layouts have clearer hierarchy and real state. USB power is separate from charging or full; unreliable USB-powered voltage is omitted.
- Browsing themes and EQ presets previews them without saving. Custom EQ has a response curve and staged audible editing; leaving a preview restores the saved sound, volume and routing remain unchanged.
- See [library discovery](library-discovery.md) and the [user guide](user-guide.md). Device installation and hardware checks are tracked in [known issues](known-issues.md).

## FreshBase 1.0.0 (unreleased)

Not released, and not yet run on a player: everything below is established by host tests, the simulator and the ARM emulator (189 checks). What differs from Podium 4.5.9 so far:

### Name and files

- **Its own name.** The firmware is FreshBase, version 1.0.0, made by Doublesharp. The start-up splash, the Home title and the USB device say FreshBase, the USB manufacturer name is Doublesharp, and the image brand and the USB storage vendor are `FRSHBS`. One file, `identity.json`, holds the names, the identifier and the version for the build, the C code and the tools, and a test fails when source spells one of them anywhere else. The USB storage revision a computer sees is the version's major.minor (`1.0`); it was a fixed `0.1`.
- **Update file `FRSHBS.IMG`.** The updater installs `FRSHBS.IMG` from the top level of internal storage. It still installs FiiO's own Echo image under FiiO's `ECHOVnnn.IMG` names, which is the way back to FiiO's firmware. It refuses the images of Podium, EchoPod and every other brand.
- **Data folder `/.freshbase`.** The player keeps its library, covers, settings and save files there, not in `/.echo`. The four-byte markers at the start of its files begin with `DS`, for the maker. Files written by Podium are not read: settings start from the defaults and the library is built again. An old `/.echo` folder can be deleted.
- **Playlist directive `#FRESHBASE-START:`.** The line the player writes above a CUE track's path in `Favorite Tracks.m3u` has this name; Podium wrote `#PODIUM-START:`.
- **Renamed from Doublesharp Echo.** Developer builds up to commit `2991a27` were called Doublesharp Echo, with the identifier `DBLSHARP`, the update file `DBLSHARP.IMG`, the data folder `/.doublesharp` and the directive `#DOUBLESHARP-START:`. FreshBase has no code for any of them: it refuses `DBLSHARP` images, does not look for `DBLSHARP.IMG`, does not read `/.doublesharp` and ignores the old directive. A player that moves over starts with the default settings, an empty listening log, no saved queue, mixtapes, favorite albums, bookmarks or Bluetooth devices, and scans its music again. The old `/.doublesharp` folders, on internal storage and on the card, can then be deleted. In `Favorite Tracks.m3u`, a CUE track saved under the old directive stands for its album's last track until the line is renamed to `#FRESHBASE-START:` on a computer. The move takes one extra install: [`device-install.md`](device-install.md), Moving from a Doublesharp Echo build.

### Sound

- **DAC identification stays in the development HAL.** Earlier developer builds exposed DAC A and DAC B in About and read both chips on the owner's player as `431980 A1.06` (CS43198). Those raw rows, USB sense and Connect USB are now removed from About; USB sense remains a debug-script operation.
- **DAC output power-up wait of 12 ms,** as in FiiO's firmware. Podium 4.5.9 waits 10 ms. An earlier build of this project with the 12 ms wait, based on Podium 4.5.5, was confirmed quiet by the owner on their Echo.

### Code space

Podium 4.5.9 has 2,392 bytes free in the 157,292-byte code window and 1,376 bytes of static RAM free when built with this project's compiler. FreshBase's release build has 16,780 and 23,128 bytes free.

- **Link-time optimisation** for the core 0 firmware (from EchoPod).
- **USB sessions and the updater run from an on-demand overlay** (from EchoPod). Mass storage, USB DAC mode, the update check and the install are loaded into high RAM once a computer is found. If the overlay cannot be loaded from either firmware slot, the player says "USB code missing: reinstall the update" and powers USB down.
- **One overlay loader** for the library rescan and USB. It reuses an intact copy already in memory, else reads firmware slot 1, else slot 2, and refuses an overlay that was linked against other code than the code that is running.
- **Size report** after every image build (from EchoPod), with its limits read from the linker maps.

### Tests

- The emulator fails when a function it replaces or watches is missing from the image; those functions are marked `CORE_SEAM` so link-time optimisation keeps them.
- Emulator scenarios for the USB overlay: loading from slot 1, reuse in memory, fallback to slot 2, and both copies damaged.

### Removed

- The recipe and patch file for an image with Chinese, Japanese and Korean glyphs. Those names show as `?`; glyph packs for them are planned ([`../ROADMAP.md`](../ROADMAP.md)).
- Podium's website, feedback form and download links.

### Not proven on a player

See [`known-issues.md`](known-issues.md).

## Gnarlyr 1.0.0 development

- Gnarlyr display and USB product name; stable internal updater and data identifiers preserve existing settings and library files.
- Five themes, configurable playback tap/hold actions, Home/Music icons and cassette artwork when a song has no cover.
- Full queue editing, journaled resume and named mixtapes, including safe cancellation and path/cue matching after rescans.
- Listening totals, top lists and export; timed sidecar lyrics.
- Demand-loaded feature screens and a bounded 16 KiB language window.
- Queue edits rebuild already-buffered next audio before output resumes.

This is a development candidate. The release checks in the repository instructions remain required before publishing a release.
