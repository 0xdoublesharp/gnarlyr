# Gnarlyr.com

Static firmware website. Serve this directory with any static host; there is no build step, dependency install or external font. Cloudflare adds its analytics beacon to served HTML. Open `index.html` directly for a local preview. The home page introduces the firmware with a Now Playing preview, three screenshot-led feature stories, benefit cards, a hardware purchase section and an installation journey. `firmware.html` covers the complete features and fixes. Hardware specifications and detailed setup stay in their dedicated guides. `docs.html` is the visual player guide and the entry point for hardware, recovery and developer references. Hardware belongs under Docs and keeps its stable `hardware.html` URL. `recovery.html` holds the detailed macOS first-installation and stock-restoration procedure; `install.html` is the short guide to preparation, music setup and later USB updates. `build.html` holds source builds, developer USB workflows and technical migration/recovery references. `languages.html` builds and checks language packs locally using `language-catalog.js` and `language-packs.js`; it works when opened directly from disk. The main home action goes to Install. All pages share `styles.css` and `site.js`. The script copies commands with success and selection feedback and opens help sections from fragment links. Instructions and expandable help remain available without JavaScript.

The cassette artwork is an original inline SVG, not a device photograph or firmware screenshot. The page includes responsive layouts, keyboard focus, reduced-motion support, readable command wrapping, and native expandable installation help. The [UX pass](../docs/gnarlyr-ux.md) records the accepted design and checks.

The Docs screenshots are product assets under `site/assets/screens/`, generated from the real firmware simulator with a silent sample library. They illustrate the interface and do not establish device behavior. Keep final public image assets under `site/assets/`; the publisher includes that directory. Do not add empty screenshot placeholders or claim that the cassette illustration is a screenshot.

DoublePaaS hosting uses the [deployment contract](../docs/deployment.md). Publish the eight HTML pages, shared CSS and JavaScript, language catalogue and builder script, favicon, robots file and sitemap together. The publisher allows those fifteen root files and optional image assets; this README and `deploy/` stay outside the served release.

Firmware feature references cover main through `ba71fdcf`; no newer firmware commits were present at the completed-features review. Public feature copy covers implemented player controls, library tools, generated Cover Flow artwork, listening exports, the battery display, audio-file validation and runtime fixes. Ogg playback integration, playback-speed work and converter implementation progress are excluded from public feature and developer pages. Keep plans and validation work in the firmware documentation and ignored notes. All twenty screenshots reflect the current interface.

Developers covers source builds, USB tools, file validation, CUE parsing, ReplayGain controls and preparing speech recordings. Features keeps the practical recommendation to convert speech below 32 kHz before copying it. Hardware ratings link to FiiO and Cirrus. Keep unrelated firmware and generated language-catalogue edits outside a website-only publication.

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

Scrolling pages use several real captures rather than a stretched image. Add one recipe per scroll position, with its own filename and key sequence but the same expected title. Keep some overlapping entries between views so the full list is covered. The optional `group` and `view_label` fields describe related captures. Add each new image to the corresponding section in `docs.html`. Settings shows every view by default with explanations beside it; Controls and EQ use expandable galleries.

The player outline is CSS around the image, so regenerated PNGs retain their original pixels and full-size links. During publication, `publish_site.py` appends each referenced asset’s content hash to its URL in the published HTML. Stylesheets, scripts, screenshots and full-size image links therefore get new browser-cache keys when their contents change without editing source pages or changing their local preview paths.


## Link preview images

Every public page has its own 1200 × 630 PNG card, Open Graph image metadata and a Twitter/X large-image card. The artwork comes from the homepage’s editable cassette SVG. Titles and descriptions in the HTML remain the source for social text; `tools/site_social.json` controls the words drawn on each image.

Regenerate images and metadata after changing that copy or the cassette artwork:

```sh
python3 tools/site_social.py
```

The renderer needs `rsvg-convert` from librsvg and Scratch mounted. Final images go under `site/assets/social/`; their filenames include a content hash so changed cards get a fresh URL. The script updates absolute `https://gnarlyr.com/` image URLs in all eight pages, keeps canonical page URLs intact and removes superseded generated cards for the configured names. Publish the site normally afterward.


## Amazon purchase links

Product links point directly to the full-size ECHO listing at `https://www.amazon.com/dp/B0GK61Y1FD?tag=doublesharp-20`. The listing’s model is ECHO with a 2.39-inch display; keep the purchase link specific to that model. Home features a purchase section with a stylized player frame, interface screenshot, hardware highlights and the main shopping button. Other pages use the shared footer shopping button, with contextual links beside hardware references.

Each page includes one short Amazon Associate disclosure beside its main purchase button. The interface avoids repeated affiliate labels. Links retain `rel="sponsored nofollow"`, and the copy explains that firmware is installed separately. There are no redirect scripts, embedded Amazon widgets, copied Amazon images or prices; pricing and availability stay on Amazon.

Homepage screenshots reuse the original simulator PNGs under `site/assets/screens/`. The surrounding player shapes are CSS illustrations. All visible features describe completed capabilities; plans and unfinished integrations stay outside the public feature copy.

When changing the product or affiliate ID, update every placement and verify the destination model on Amazon.
