# Gnarlyr.com

Static firmware website. Serve this directory with any static host; there is no build step, dependency install or external font. Cloudflare adds its analytics beacon to served HTML. Open `index.html` directly for a local preview. The home page introduces Gnarlyr; `firmware.html` covers features and fixes. `docs.html` is the visual player guide and the entry point for hardware, recovery and developer references. Hardware belongs under Docs and keeps its stable `hardware.html` URL. `recovery.html` holds the detailed macOS first-installation and stock-restoration procedure; `install.html` is the short guide to preparation, music setup and later USB updates. `build.html` holds source builds, developer USB workflows and technical migration/recovery references. `languages.html` builds and checks language packs locally using `language-catalog.js` and `language-packs.js`; it works when opened directly from disk. The main home action goes to Install. All pages share `styles.css` and `site.js`. The script copies commands with success and selection feedback and opens help sections from fragment links. Instructions and expandable help remain available without JavaScript.

The cassette artwork is an original inline SVG, not a device photograph or firmware screenshot. The page includes responsive layouts, keyboard focus, reduced-motion support, readable command wrapping, and native expandable installation help. The [UX pass](../docs/gnarlyr-ux.md) records the accepted design and checks.

The Docs screenshots are product assets under `site/assets/screens/`, generated from the real firmware simulator with a silent sample library. They illustrate the interface and do not establish device behavior. Keep final public image assets under `site/assets/`; the publisher includes that directory. Do not add empty screenshot placeholders or claim that the cassette illustration is a screenshot.

DoublePaaS hosting uses the [deployment contract](../docs/deployment.md). Publish the eight HTML pages, shared CSS and JavaScript, language catalogue and builder script, favicon, robots file and sitemap together. The publisher allows those fifteen root files and optional image assets; this README and `deploy/` stay outside the served release.

Firmware references cover main through `d8b30ba4`, the active `gnarlyr` audio branch through `f0cc9d9e`, and the separate Opus decoder/image-bank components through `279a3222`. The language-pack builder, inspect/edit/download flow and USB management tools come from `dbc20c7e`. The key-lock source fix in `91c08976` resets an interrupted hold before a lock toggle. Listening navigation has regression coverage for returning with one Back after repeated Play. The settings source previews display and audio choices until confirmed; leaving the picker restores the saved value. Ignored device records confirm external Spanish loading and return to English; no reviewed Spanish translation download is published here.

Developers describes Vorbis integration, prepared ReplayGain metadata and persistent normalization controls, engine-connected pitch-preserving tempo, and the separate Opus decoder/program. Tempo controls and full Opus playback remain separate integration work. Hardware reports identify low-rate conversion dropouts, so Features recommends converting speech below 32 kHz to 44.1 or 48 kHz before copying it. Hardware ratings link to FiiO and Cirrus; device readbacks retain their unit scope. Keep uncommitted firmware and generated language-catalogue changes outside a website-only publication.

The first public firmware release will be Gnarlyr 1.0, using the exact image name `GNARLYR.img`. Publish a release version and download only when the firmware is ready. Firmware now uses the canonical Gnarlyr identity. This website task does not change firmware or tooling; the developer guide points to the matching identity-transition procedure for older custom builds.

The page provides source-build instructions. When a public Gnarlyr release is ready, add the fork’s actual source/release URL, image version, and published SHA-256; link the release’s matching installation/recovery documentation. Do not substitute upstream Podium downloads for this fork. Keep plans and status in ignored `notes/`; commit reference documentation and keep public pages focused on features and usage.

## Regenerate the Docs screenshots

From the repository root, with Scratch mounted and the firmware prerequisites installed:

```sh
python3 tools/site_screenshots.py
```

The script builds `echo_sim` through `tools/firmware.py`, creates silent FLAC music and a chaptered M4B on Scratch, indexes them with the firmware's library tool, and navigates the simulator. It never connects to a player. The final PNGs go to `site/assets/screens/`; builds, demo files and intermediate BMPs stay on Scratch. Successful runs remove their temporary workspace; failed runs print its location for inspection.

Configure the sample music and chapter names, battery level, frame count, key sequences, expected titles and image dimensions in `tools/site_screenshots.json`. The `{firmware_name}` token follows `firmware/identity.json`, so screen titles and demo artists track the configured firmware name. Every capture starts with its own clean settings and library state. A screenshot is accepted only when the simulator reports the expected screen title and the image has the expected size. All selected scenes must render before existing product images are replaced.

```sh
python3 tools/site_screenshots.py --list
python3 tools/site_screenshots.py --only home --only themes
python3 tools/site_screenshots.py --no-build --only equalizer
python3 tools/site_screenshots.py --config /path/to/scenes.json --output /Volumes/Scratch/SnowskyEcho/reports/docs-preview
```

`--only` can be repeated. `--no-build` explicitly reuses the existing simulator; omit it after firmware changes. `--simulator` selects a different host simulator binary. `--keep-work` retains intermediate files on Scratch. Key sequences use `up`, `down`, `play`, `menu`, `back` and `power`, optionally followed by `:long` or `:held`. Update a scene's route when a menu changes; the expected-title check prevents silently publishing a screenshot of the wrong screen. The script returns JSON for both success and errors.
