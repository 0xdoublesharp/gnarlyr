# Gnarlyr.com

Static firmware website. Serve this directory with any static host; there is no build step, dependency install or external font. Cloudflare adds its analytics beacon to served HTML. Open `index.html` directly for a local preview. The home page introduces the firmware with a Now Playing preview, three screenshot-led feature stories, benefit cards, a hardware purchase section and an installation journey. `firmware.html` covers the complete features and fixes. `companion.html` introduces the desktop beta with library browsing, reviewed and verified music copies, player controls, firmware review and language packs. Hardware specifications and detailed setup stay in their dedicated guides. `docs.html` is the visual player guide and the entry point for hardware, recovery and developer references. Hardware belongs under Docs and keeps its stable `hardware.html` URL. `install.html` covers ordinary first installation, music setup and later USB updates. `recovery.html` covers macOS recovery when the player will not start or expose its drives, with verified Gnarlyr and stock-restoration writes. `build.html` holds source builds, developer USB workflows and technical migration/recovery references. `languages.html` builds and checks language packs locally using `language-catalog.js` and `language-packs.js`; it works when opened directly from disk. The main home action goes to Install. All pages share `styles.css` and `site.js`. The script copies commands with success and selection feedback and opens help sections from fragment links. Instructions and expandable help remain available without JavaScript.

The cassette artwork is an original inline SVG, not a device photograph or firmware screenshot. The page includes responsive layouts, keyboard focus, reduced-motion support, readable command wrapping, and native expandable installation help. The [UX pass](../docs/gnarlyr-ux.md) records the accepted design and checks.

The Docs screenshots are product assets under `site/assets/screens/`, generated from the real firmware simulator with a silent sample library. They illustrate the interface and do not establish device behavior. Keep final public image assets under `site/assets/`; the publisher includes that directory. Do not add empty screenshot placeholders or claim that the cassette illustration is a screenshot.

DoublePaaS hosting uses the [deployment contract](../docs/deployment.md). Publish the eight public HTML pages, shared CSS and JavaScript, language catalogue and builder script, favicon, robots file and sitemap together. The publisher allows those fifteen root files and optional image assets; this README and `deploy/` stay outside the served release.

Firmware feature copy follows `main` through `40a6f193`, including scan-phase progress, panel DMA, screen-off scheduling, wireframe start/stop, card startup and the USB reconnect cycle. All 71 firmware images use capture baseline `8bbd8f35`, including the Power animation setting, USB fixed-width figures and updated headers. Simulator scenes check the screen title and dimensions; USB scenes come from the native renderer. This includes four Now Playing styles, data-only theme packs, crossfeed and speech controls, hide/delete track actions, language packs and USB session statistics, alongside the existing listening and playback features. Installation references cover trial confirmation and rollback; known USB-audio acceptance limits stay in Developers. Technical hardware documentation explains the two cores, DMA, reusable memory banks and analog-output changes; host and physical audio acceptance stays in the developer guide. Home puts Clock above USB mode, and storage guidance covers Gnarlyr internal storage, newly formatted GnarlySD cards and preserved custom labels. The seven Settings views and the lower Quick Settings view remain visible by default. The browser language catalogue is exported from the same firmware snapshot. Keep plans and validation work in firmware documentation and ignored notes; simulator captures show the interface, not physical-device validation.

Ogg playback and unverified converter performance stay outside public playback claims. Playback-speed controls, A–B repeat, end-of-track sleep, artist album strips and Recap follow the integrated source; physical playback acceptance is described in Developers. Features keeps the practical recommendation to convert speech below 32 kHz before copying it. Developers covers source builds, USB tools, clock sync, file validation, CUE parsing and ReplayGain. Hardware ratings link to FiiO and Cirrus. Keep unrelated firmware edits outside a website-only publication. Export the browser language catalogue from the capture source when its menus or pack limits change.

The install page links the public beta, 1.0.0-beta.1, by its pinned release URL: `ECHOECHO.IMG` for the first install from FiiO's firmware, `GNARLYR.IMG` for later updates. A pre-release is never GitHub's `latest`, so keep beta links pinned to their tag. Publish the site's beta links together with the release. Firmware now uses the canonical Gnarlyr identity.

The public firmware source is https://github.com/0xdoublesharp/gnarlyr. When a public Gnarlyr release is ready, add its actual release URL, image version, and published SHA-256; link the release’s matching installation/recovery documentation. Do not substitute upstream Podium downloads for this fork. Keep plans and status in ignored `notes/`; commit reference documentation and keep public pages focused on features and usage.

## Regenerate the Docs screenshots

From the repository root, with Scratch mounted and the firmware prerequisites installed:

```sh
python3 tools/site_screenshots.py --source-root /path/to/checkout-containing-8bbd8f35
```

The script builds `echo_sim` through `tools/firmware.py`, creates synthetic FLAC sample music and a chaptered M4B on Scratch, indexes them with the firmware's library tool, and navigates the simulator. It never connects to a player. The final PNGs go to `site/assets/screens/`; builds, demo files and intermediate BMPs stay on Scratch. Successful runs remove their temporary workspace; failed runs print its location for inspection.

Configure the sample music and chapter names, battery level, frame count, key sequences, expected titles and image dimensions in `tools/site_screenshots.json`. The `{firmware_name}` token follows `firmware/identity.json`, so screen titles and demo artists track the configured firmware name. Every capture starts with its own clean settings and library state. A screenshot is accepted only when the simulator reports the expected screen title and the image has the expected size. All selected scenes must render before existing product images are replaced. `demo.audio_source` chooses silence or a synthetic stereo tone; the tone gives the VU and Spectrum views real sample levels without using third-party music. `theme_example` builds a named folder from the selected source’s examples/themes into an isolated sample card. `native_snapshot` selects an exact PPM emitted by the existing test_usb_screen native renderer; the script builds/runs that renderer for USB scenes, converts pixels directly and checks dimensions. Native USB captures use sample session values and are labeled that way in Docs. Their source test verifies the rendered state; simulator scenes retain their expected-title check.

```sh
python3 tools/site_screenshots.py --list
python3 tools/site_screenshots.py --only home --only themes
python3 tools/site_screenshots.py --no-build --only equalizer
python3 tools/site_screenshots.py --config /path/to/scenes.json --output /Volumes/Scratch/SnowskyEcho/reports/docs-preview
```

`--source-root` builds the simulator, indexes the sample library and compiles language examples from the selected checkout. Use a checkout containing `8bbd8f35` for the current menu recipes; main already includes it. `--only` can be repeated. `--no-build` explicitly reuses the existing simulator and native USB renderer; omit it after firmware changes. `--simulator` selects a different host simulator binary. `--keep-work` retains intermediate files on Scratch. Key sequences use `up`, `down`, `play`, `menu`, `back` and `power`, optionally followed by `:long` or `:held`. Update a scene's route when a menu changes; the expected-title check prevents silently publishing a screenshot of the wrong screen. The script returns JSON for both success and errors.

Scrolling pages use several real captures rather than a stretched image. Add one recipe per scroll position, with its own filename and key sequence but the same expected title. Keep some overlapping entries between views so the full list is covered. The optional `group` and `view_label` fields describe related captures. Add each new image to the corresponding section in `docs.html`. Settings, Controls and EQ show every screenshot view without expandable galleries. Settings includes explanations beside each view.

Language scenes include a `language` object with a short `code` and a `strings` map of firmware string IDs to translated labels. Their key sequence selects that pack through Settings → Language before visiting the captured screen. The script builds a pack against the current English catalogue, checking placeholders, glyph coverage and pack size with the firmware's own tool, then confirms the selected code was saved. The Spanish, French, German and Russian examples translate the Home labels shown; other labels use the normal English fallback. These small demo packs are capture inputs, not full downloadable translations. Regenerate them with `--only home-es --only home-fr --only home-de --only home-ru`.

The player outline is CSS around the image, so regenerated PNGs retain their original pixels and full-size links. During publication, `publish_site.py` appends each referenced asset’s content hash to its URL in the published HTML. Stylesheets, scripts, screenshots and full-size image links therefore get new browser-cache keys when their contents change without editing source pages or changing their local preview paths.


## Link preview images

Every public page has its own 1200 × 630 PNG card, Open Graph image metadata and a Twitter/X large-image card. The artwork comes from the homepage’s editable cassette SVG. Titles and descriptions in the HTML remain the source for social text; `tools/site_social.json` controls the words drawn on each image.

Regenerate images and metadata after changing that copy or the cassette artwork:

```sh
python3 tools/site_social.py
```

The renderer needs `rsvg-convert` from librsvg and Scratch mounted. Final images go under `site/assets/social/`; their filenames include a content hash so changed cards get a fresh URL. The script updates absolute `https://gnarlyr.com/` image URLs in all eight pages, keeps canonical page URLs intact and removes superseded generated cards for the configured names. Publish the site normally afterward.


## Purchase links

Product links point directly to the full-size ECHO listing at `https://www.amazon.com/dp/B0GK61Y1FD?tag=doublesharp-20`. The listing’s model is ECHO with a 2.39-inch display; keep the purchase link specific to that model. Home features a purchase section with a stylized player frame, interface screenshot, hardware highlights and retailer purchase buttons. Other pages use the shared footer retailer buttons, with contextual links beside hardware references. AliExpress appears beside Amazon in the main shopping section and every footer. PandaG Store is the main AliExpress destination. Hardware groups all three supplied AliExpress listings beside Amazon and labels their capacity options.

Each page includes one short disclosure covering Amazon and AliExpress beside its purchase buttons. The interface avoids repeated affiliate labels. Links retain `rel="sponsored nofollow"`, and the copy explains that firmware is installed separately. There are no redirect scripts, embedded retailer widgets, copied retailer images or prices; pricing and availability stay on the retailers’ pages.

Home links to the stock comparison in `firmware.html#over-stock` and the hardware changes in `hardware.html#unlocked`. The comparison uses FiiO ECHO 2.0.0: M3U playlists, song favorites, EQ, gain, five DAC filters, Bluetooth and USB DAC mode are existing stock capabilities. Hardware copy describes firmware controls and mechanisms; it makes no measured battery-life, analog-quality or output-power improvement claim.

Homepage screenshots reuse the original simulator PNGs under `site/assets/screens/`. The surrounding player shapes are CSS illustrations. All visible features describe completed capabilities; plans and unfinished integrations stay outside the public feature copy.

When changing a product or affiliate link, update every placement and verify the destination model and capacity.


## Awin purchase links

Doublesharp’s Awin publisher ID is `3152908`. Network account activation and advertiser-program approval are separate. Add a retailer to the public site after its program appears under Joined and the product’s eligibility is confirmed.

| Advertiser | Awin merchant ID | Full-size ECHO destination |
| --- | --- | --- |
| Advanced MP3 Players / AMP3 | `361` | `https://www.advancedmp3players.co.uk/products/fiio-snowsky-echo-portable-hi-res-lossless-music-player` |
| AliExpress US | `6378` | `https://www.aliexpress.us/item/3256811599037316.html` |

FiiO’s ECHO launch announcement links to the AliExpress destination above. Confirm that regional destination and the exact item in Awin’s Link Builder before publishing an AliExpress link.

Awin deep links use `https://www.awin1.com/cread.php` with `awinmid` for the merchant, `awinaffid=3152908`, an optional placement-specific `clickref`, and a URL-encoded product destination in `ued`. Use the approved advertiser’s Link Builder output for the final public link. Keep links labelled by retailer, use `rel="sponsored nofollow"`, and retain one concise disclosure per page covering all purchase links.

References: [Awin deep-link format](https://success.awin.com/articles/en_US/Knowledge/What-is-deep-linking-and-why-should-I-use-this), [advertiser enrollment](https://success.awin.com/s/article/How-do-I-join-an-advertiser-programme?language=en_US), [AMP3 program](https://ui.awin.com/merchant-profile/361), [AliExpress US program](https://ui.awin.com/merchant-profile/6378), [FiiO ECHO listing](https://www.fiio.com/newsinfo/1120734.html).


## AliExpress Portals links

The site uses direct AliExpress Portals promotion links generated by the owner with Tracking ID `default`. These are separate from Awin and do not use the Awin publisher ID.

| Seller and capacity options | Product page | Promotion link |
| --- | --- | --- |
| PandaG Store · 4/8 GB | `https://www.aliexpress.us/item/3256812433216354.html` | `https://s.click.aliexpress.com/e/_c3RytQbH` |
| Stone Digital Store · 4/8 GB | `https://www.aliexpress.us/item/3256811570830050.html` | `https://s.click.aliexpress.com/e/_c3l6aw9p` |
| Stars 3c Store · 8 GB | `https://www.aliexpress.us/item/3256811620516629.html` | `https://s.click.aliexpress.com/e/_c3VNY3EX` |

The supplied product screenshots identify all three as the full-size ECHO. Keep the short promotion links exactly as generated, label capacity where shown, and do not describe the sellers as FiiO’s official store. Product photos, sale prices and commission percentages are not copied into the website.

## Companion screenshots and availability

The Companion page is temporarily hidden. Its HTML, screenshots and social card remain in source but are excluded from publication; navigation, promotions and the sitemap omit it. The desktop screenshots are product assets under `site/assets/companion/`; they use a sample music library and synthetic player folders, never the owner’s player. Desktop images use a window frame rather than the firmware’s player outline.

```sh
node tools/site_companion_screenshots.mjs --source-root /path/to/gnarlyr-companion
```

Without `--source-root`, the script uses `../gnarlyr-companion` beside this checkout.

Requires Node 24+, ffmpeg and the companion’s installed dependencies. `tools/site_companion_screenshots.json` configures the viewport, sample albums and six captured views. The script rebuilds the app, launches its actual Electron interface with isolated Scratch user data, drives the GUI and confirms that sync preview changes no files, settings stay unchanged and no firmware is staged. All six views render before public images are replaced. `--no-build` explicitly reuses an existing build; `--config` selects a different capture configuration. Dependencies come from the companion checkout’s existing installation. Source for the published captures is companion commit `899a0b9`.

The desktop app’s source is in the private `gnarlyr-desktop` repository, usually checked out as `../gnarlyr-companion`. The owner selected a separate public GitHub releases-only repository for future installers and release notes, now at https://github.com/0xdoublesharp/gnarlyr/releases. Add real, versioned installer links when packages are ready; the current page is a beta preview. The companion’s distribution terms remain separate from the firmware’s GPL license. Music copying reviews the missing files, preserves existing files and verifies each completed copy. Cancellation keeps verified files; copying does not build the playback database or eject the drives. Automatic sync and companion playlist/mixtape editing remain outside the public claims.

The browser language builder and desktop importer support the firmware’s initial segmented version 1 pack format. Checks cover file size, section checksums, text/glyph validity and peak player-memory use. Export the catalogue from the matching firmware source and run `test_language_browser.py` against that source; `SITE_SOURCE_ROOT` selects another checkout.
