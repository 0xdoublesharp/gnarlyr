# Doublesharp Echo release notes

Doublesharp Echo starts from Podium 4.5.9 ([`../UPSTREAM.md`](../UPSTREAM.md)). For everything up to and including that version, see [Podium's changelog](https://podium-echo.online/changelog.html).

## Doublesharp Echo 1.0.0 (unreleased)

Not released, and not yet run on a player: everything below is established by host tests, the simulator and the ARM emulator (186 checks). What differs from Podium 4.5.9 so far:

### Name and files

- **Its own name.** The firmware is Doublesharp Echo, version 1.0.0. The start-up splash and the USB device say Doublesharp Echo, the Home title says Doublesharp, and the image brand and the USB storage vendor are `DBLSHARP`. One file, `identity.json`, holds the names, the identifier and the version for the build, the C code and the tools.
- **Update file `DBLSHARP.IMG`.** The updater installs `DBLSHARP.IMG` from the top level of internal storage. It still installs FiiO's own Echo image under FiiO's `ECHOVnnn.IMG` names, which is the way back to FiiO's firmware. It refuses the images of Podium, EchoPod and every other brand.
- **Data folder `/.doublesharp`.** The player keeps its library, covers, settings and save files there, not in `/.echo`, and its file magics start with `DS`. Files written by Podium are not read: settings start from the defaults and the library is built again. An old `/.echo` folder can be deleted.
- **Playlist directive `#DOUBLESHARP-START:`.** The line the player writes above a CUE track's path in `Favorite Tracks.m3u` has this name; Podium wrote `#PODIUM-START:`.

### Sound

- **DAC output power-up wait of 12 ms,** as in FiiO's firmware. Podium 4.5.9 waits 10 ms. An earlier build of this project with the 12 ms wait, based on Podium 4.5.5, was confirmed quiet by the owner on their Echo.

### Code space

Podium 4.5.9 has 2,392 bytes free in the 157,292-byte code window and 1,376 bytes of static RAM free when built with this project's compiler. Doublesharp's release build has 19,396 and 23,248 bytes free.

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
