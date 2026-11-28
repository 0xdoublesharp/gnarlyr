# Changelog

Changes to Gnarlyr firmware for the full-size SNOWSKY ECHO, its website, and its tools. Release dates use GitHub’s UTC publication dates.

## [1.0.0-beta.3] - 2026-11-27

- **Headset volume holds.** In **Settings → Buttons → Headset Vol hold**, choose **Skip tracks** (the default) or **Repeat volume** for a supported 3.5 mm inline remote. Taps still change volume one step. Physical remote checks are pending. Updated all six official language packs to match the 978-string catalog. [Issue #1](https://github.com/0xdoublesharp/gnarlyr/issues/1).
- **Browser language-pack builder.** The [language page](https://gnarlyr.com/languages.html) now builds and inspects packs that need an `extra.font` file, including packs for Polish, Korean, and other languages with glyphs missing from the built-in fonts. [Issue #2](https://github.com/0xdoublesharp/gnarlyr/issues/2).
- **Documentation.** Refreshed the firmware guides in `firmware/docs/`, upstream and licensing details in `firmware/UPSTREAM.md` and `firmware/CREDITS.md`, the file-format reference in `docs/formats.md`, and the firmware comparison in `site/comparison.html`.

## [1.0.0-beta.2] - 2026-11-27

### Added

- **Battery time remaining.** The estimate learns from your usage. Choose **Time left** under **Settings → Display & text → Status bar → Battery readout**, or view and reset the estimate under **Settings → Power & battery → Battery time**. Player checks are pending.
- **Theme pack rendering.** The `stage` module applies the `.gtheme` v2 `style` table’s scene geometry and refresh interval to VU meters and Spectrum. The `playing` module draws 160 × 48 `cassette_label` artwork from slot 5 in Cassette view. Both changes still need player checks.
- **Theme Studio.** Import, edit, preview, and export `.gtheme` v2 packs in `studio.html`. Play your own song locally to drive the visualizers. Run the stress test for a contact sheet of screen previews and check text contrast against the artwork pixels behind it. See the encoded pack size and emulated CPU cost benchmarks for palette-only and indexed Now Playing artwork.
- **Theme sharing.** Share palette and style through `#p=` links with 60-character tokens. Export a 1200 × 630 showcase PNG or a source ZIP. The gallery’s `themes-catalog.json` supplies creator credits and theme metadata, including checksums and download URLs for Companion. Umami uses `?ref=` creator handles to attribute downloads, exports, and shares.

### Changed

- Updated Vice 86 to v1.0.1 with custom visualizer styling and cassette labels for both orientations. Updated all six official language packs (`de`, `es`, `fr`, `ja`, `pt`, and `zh`) for the battery controls.
- Moved About to the `format` module to reclaim memory headroom in `views`. Moved clock setup, progress saving, and settings decoding into their owning modules.
- Documented podcast and audiobook playback, 3.5 mm inline headset remotes, and returning to FiiO firmware. Refreshed Companion screenshots and updated the firmware comparison for EchoPod 0.13.2.
- Expanded Umami coverage to downloads, outbound links, and site controls. Collection respects Do Not Track and Global Privacy Control.

### Fixed

- Reject malformed FAT long filenames that could corrupt memory while reading a drive.
- Keep album and artist text readable in Cover Flow when a custom theme uses dark title text.
- Removed the misleading **Medium** text-size value from **Settings → Display & text** and added submenu chevrons to cassette rows in **Add to tape** and to existing **Parametric EQ** filters.
- Adjusted Theme Studio’s Cassette preview to keep track text clear of custom labels in landscape and portrait. Long text now ends with an ellipsis when space runs out.
- Corrected the feature, player, hardware, and comparison guides to match beta.1 behavior and flag outstanding player checks.

## [1.0.0-beta.1] - 2026-11-26

First public beta. See the [beta.1 release notes] for installation, downloads, and the full list of player checks.

### Added

- Shipped VU meters and format, volume, and buffer details for USB DAC, plus a live transfer graph for USB storage.
- Added trial firmware updates with a backup of the previous version, plus safe microSD eject and card detection while the player is running. Automatic rollback still needed a player check at release.
- Shipped Recap listening stats, five built-in themes with `.gtheme` pack support, portrait orientation, a customizable Home screen, and wireframe startup and shutdown animations.
- Included six official language packs alongside built-in English. The beta also included audiobook chapters, podcast and audiobook sound controls, parametric EQ with AutoEQ import, ReplayGain, and four headphone profiles.

### Changed

- Enabled High Speed USB transfers. On the developer’s ECHO, microSD transfers reached 8.7 MB/s writing and 9.8 MB/s reading.
- Showed progress during library scans, made tracks available before cover art finished loading, and reused tags for unchanged files. Tag reuse after file changes still needed a player check at release.
- Lowered idle and standby clocks and added a lower-clock mode for supported screen-off playback. The clock change and Apple Lossless accelerator still needed direct player checks, and battery savings had yet to be measured.

## [1.0.0-alpha.2] - 2026-11-26

Alpha for end-to-end Companion testing. See the [alpha.2 release notes].

### Added

- Published six language packs with the alpha and showed how to install more languages in **Settings → Language** when only English is installed.

### Fixed

- Excluded invisible characters such as zero-width joiners, directional marks, and variation selectors from missing-font reports.
- Made the macOS FAT32 first-install tool more reliable when placing the image at the end of the volume and documented FiiO’s 2.0.0 updater behavior.

## [1.0.0-alpha.1] - 2026-11-25

### Added

- Published the first alpha for testing Companion’s first-install and update workflows on the full-size ECHO. The release included both firmware images, matching source, and checksums.

[1.0.0-beta.3]: https://github.com/0xdoublesharp/gnarlyr/compare/v1.0.0-beta.2...v1.0.0-beta.3
[1.0.0-beta.2]: https://github.com/0xdoublesharp/gnarlyr/compare/v1.0.0-beta.1...v1.0.0-beta.2
[1.0.0-beta.1]: https://github.com/0xdoublesharp/gnarlyr/compare/v1.0.0-alpha.2...v1.0.0-beta.1
[1.0.0-alpha.2]: https://github.com/0xdoublesharp/gnarlyr/compare/v1.0.0-alpha.1...v1.0.0-alpha.2
[1.0.0-alpha.1]: https://github.com/0xdoublesharp/gnarlyr/releases/tag/v1.0.0-alpha.1
[beta.1 release notes]: https://github.com/0xdoublesharp/gnarlyr/releases/tag/v1.0.0-beta.1
[alpha.2 release notes]: https://github.com/0xdoublesharp/gnarlyr/releases/tag/v1.0.0-alpha.2
