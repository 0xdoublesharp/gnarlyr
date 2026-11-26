# Gnarlyr.com

Open `index.html` directly to preview the English site. The site is static HTML, CSS, and JavaScript with no dependency install or external fonts. The Python publisher generates translated homepages from the English source. See [Website languages](#website-languages).

| Page | What it covers |
| --- | --- |
| [Home](index.html), [Firmware](firmware.html), and [Comparison](comparison.html) | Gnarlyr's features and how they compare with stock firmware |
| [Docs](docs.html), [Install](install.html), and [Recovery](recovery.html) | Using the player, installing and updating firmware, and recovering the full-size ECHO |
| [Hardware](hardware.html) and [Build](build.html) | ECHO specifications, source builds, and developer tools |
| [Companion](companion.html), [Languages](languages.html), and [Themes](themes.html) | The desktop beta, browser language builder, and theme packs |
| [Theme Studio](studio.html) | RGB565 palette, artwork, icon and pack editor |

All pages share `styles.css` and `site.js`. The script copies commands and opens linked help sections. The installation steps and expandable help also work without JavaScript. The home page uses the Sunset Drive horizon and simulator captures of Now Playing.

Use the [deployment contract](../docs/deployment.md) to verify or publish a site change. The publisher includes the public files listed in `tools/publish_site.py`, generated locale pages, and images under `site/assets/`. Cloudflare adds its analytics beacon to served HTML.

Link to [Gnarlyr Releases](https://github.com/0xdoublesharp/gnarlyr/releases) until real download assets are available. When adding a firmware download, use the actual release URL, version, and published SHA-256, and link the matching install and recovery instructions. `ECHOECHO.img` is the release filename for a first install and also works for later updates. Source builds produce `GNARLYR.IMG`. [Download reference](../docs/downloads.md).

## Regenerate the Docs screenshots

From the repository root, with Scratch mounted and the firmware prerequisites installed:

```sh
compute run --class moderate --where auto --sync . \
  --exclude /.local/ --exclude /notes/ --exclude /firmware/build/ \
  --artifacts site/assets/screens -- python3 tools/site_screenshots.py
```

The script builds `echo_sim` through `tools/firmware.py` and honors `COMPUTE_JOBS` for build parallelism. It creates synthetic FLAC sample music and a chaptered M4B on Scratch, indexes them with the firmware's library tool, and navigates the simulator. Final PNGs go to `site/assets/screens/`. Builds, demo files, and intermediate BMPs stay on Scratch. A successful run removes its temporary workspace. A failed run prints the path for inspection. The captures illustrate the interface using sample data. For device checks, see the [hardware playbooks](../docs/hardware-playbooks.md).

Configure the sample music and chapter names, battery level, frame count, key sequences, expected titles and image dimensions in `tools/site_screenshots.json`. The `{firmware_name}` token follows `firmware/identity.json`, so screen titles track the configured firmware name. Every capture starts with its own clean settings and library state. A screenshot is accepted only when the simulator reports the expected screen title and the image has the expected size. All selected scenes must render before existing product images are replaced. `demo.audio_source` chooses silence or a synthetic stereo tone; the tone gives the VU and Spectrum views real sample levels without using third-party music. `theme_example` builds a named folder from the selected source’s examples/themes into an isolated sample card. `native_snapshot` selects an exact PPM emitted by the existing test_usb_screen native renderer; the script builds/runs that renderer for USB scenes, converts pixels directly and checks dimensions. Native USB captures use sample session values and are labeled that way in Docs. Their source test verifies the rendered state; simulator scenes retain their expected-title check.

```sh
python3 tools/site_screenshots.py --list
python3 tools/site_screenshots.py --only home --only themes
python3 tools/site_screenshots.py --no-build --only equalizer
python3 tools/site_screenshots.py --config /path/to/scenes.json --output /Volumes/Scratch/SnowskyEcho/reports/docs-preview
```

`--source-root` builds the simulator, indexes the sample library and compiles language examples from the selected checkout. Use a checkout whose menus match the configured scenes. `--only` can be repeated. `--no-build` explicitly reuses the existing simulator and native USB renderer; omit it after firmware changes. `--simulator` selects a different host simulator binary. `--keep-work` retains intermediate files on Scratch. Key sequences use `up`, `down`, `play`, `menu`, `back`, and `power`, optionally followed by `:long` or `:held`. Prefix `play+` or `menu+` for a chord; a capture can stop during a held chord to show its preview. Update a scene's route when a menu changes; the expected-title check prevents silently publishing a screenshot of the wrong screen. The script returns JSON for both success and errors.

Scrolling pages use several real captures rather than a stretched image. Add one recipe per scroll position, with its own filename and key sequence but the same expected title. Keep some overlapping entries between views so the full list is covered. The optional `group` and `view_label` fields describe related captures. Add each new image to the corresponding section in `docs.html`. Settings, Controls and EQ show every screenshot view without expandable galleries. Settings includes explanations beside each view.

Language scenes include a `language` object with a short `code` and a `strings` map of firmware string IDs to translated labels. Their key sequence selects that pack through Settings → Language before visiting the captured screen. The script builds a pack against the current English catalogue, checking placeholders, glyph coverage and pack size with the firmware's own tool, then confirms the selected code was saved. The Spanish, French, German and Russian examples translate the Home labels shown; other labels use the normal English fallback. These small demo packs are capture inputs, not full downloadable translations. Regenerate them with `--only home-es --only home-fr --only home-de --only home-ru`.

The player outline is CSS around the image, so regenerated PNGs retain their original pixels and full-size links. During publication, `publish_site.py` appends each referenced asset’s content hash to its URL in the published HTML. Stylesheets, scripts, screenshots and full-size image links therefore get new browser-cache keys when their contents change without editing source pages or changing their local preview paths.


## Website languages

English HTML remains the editable source. The publisher generates complete
homepages at `/es/`, `/de/`, `/ja/`, `/fr/`, and `/pt/` from that same layout.
The language picker works without JavaScript. Other pages retain their English
URLs. On those pages, the picker explains that other languages open the
translated homepage. Screenshots and the shared social images remain in English.

The initial languages are Spanish, German, Japanese, French, and Portuguese.
They are the five most-used website content languages after English in the
[W3Techs survey](https://w3techs.com/technologies/overview/content_language),
checked November 21, 2026. This measures web presence, not Gnarlyr traffic or
search demand. Portuguese uses Brazilian wording with a language-wide `pt` URL.

`i18n/locales.json` declares the languages and the pages ready for publication.
`i18n/LOCALE.json` contains translations, the English source text, and its SHA-256.
These catalogues are source files and are never published. Stable `data-i18n`
IDs keep a paragraph, including its inline links, together. Attributes have
separate IDs. IDs stay in English source and are removed from published HTML.

After editing translated copy:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/site_locales.py status
```

The command lists only missing, changed, or invalid entries, including their
previous and current English text. Update those translations and hashes while
keeping unchanged entries. Publication refuses missing or stale translations
and changes to protected HTML markup or links. It also refuses new text that
has not been marked for translation.

For a new paragraph, or to prepare another page:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/site_locales.py annotate --page index.html
PYTHONDONTWRITEBYTECODE=1 python3 tools/site_locales.py catalogue --page index.html
```

The first command adds IDs without changing existing ones. The second exports
the English text and hashes for translation. Add another page to `pages` only
when its complete catalogue is ready in all five languages. Interactive guides
also need their JavaScript labels translated before enabling them. Review
installation and recovery instructions against the English source.

Each translated page has its own canonical URL, reciprocal `hreflang` links,
an English `x-default`, localized search and social metadata, and a `WebSite`
structured-data entry on each homepage. The generated sitemap lists canonical
pages only. There are no language-detection redirects or English guide copies
under locale paths. Analytics allows only the explicit published routes.

For a review build, use a task-owned Scratch directory:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/site_locales.py render --output /Volumes/Scratch/SnowskyEcho/reports/site-i18n/public
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_*site*.py'
node --test tests/test_site_analytics.mjs tests/test_theme_studio_codec.mjs
```

The normal publisher builds language pages in memory, so translated HTML and
copies of shared assets are not committed. The static runtime is unchanged.

## Theme Studio and gallery

Use the review build above to preview Studio. The publisher bundles its modules, the Inter bitmap faces and the source folders behind the gallery presets. The code and fonts are served by the site. Imported artwork and audio are processed in the browser.

Curators maintain creator profiles, remix lineage and optional aggregate counts in `themes-catalog.json`. The gallery shows counts once they are available. A gallery entry's source points at an allowlisted example folder. Add source assets under `examples/themes/` and allowlist the folder in the publisher when adding a theme.

## Link preview images

Each English page has a 1200 × 630 PNG card for Open Graph and Twitter/X. Cards pair the Sunset Disc with a firmware or Companion screenshot. Translated homepages share the homepage image and localize its title, description, and alt text. Set the social title and description in the HTML. Use `tools/site_social.json` to choose the screenshot, describe it for the image alt text, and set the words drawn on the card.

Regenerate images and metadata after changing that copy or the Sunset Disc artwork:

```sh
python3 tools/site_social.py
```

The renderer needs `rsvg-convert` from librsvg and Scratch mounted. Final images go under `site/assets/social/`; their filenames include a content hash so changed cards get a fresh URL. The script updates absolute `https://gnarlyr.com/` image URLs on every public page, keeps canonical page URLs intact and removes superseded generated cards for the configured names. Publish the site normally afterward.


## Purchase links

Product links point directly to the Snowsky ECHO listing at `https://www.amazon.com/dp/B0GK61Y1FD?tag=doublesharp-20`. The listing’s model is ECHO with a 2.39-inch display; keep the purchase link specific to that model. Home features a purchase section with a stylized player frame, interface screenshot, hardware highlights and retailer purchase buttons. Other pages use the shared footer retailer buttons, with contextual links beside hardware references. AliExpress appears beside Amazon in the main shopping section and every footer. PandaG Store is the main AliExpress destination. Hardware groups all three supplied AliExpress listings beside Amazon and labels their capacity options.

Each page includes one short disclosure covering Amazon and AliExpress beside its purchase buttons. The interface avoids repeated affiliate labels. Links retain `rel="sponsored nofollow"`, and the copy explains that firmware is installed separately. There are no redirect scripts, embedded retailer widgets, or copied retailer images. The Comparison page shows dated USD price references from sources other than Amazon and links to retailers for current pricing and availability.

Home links to `firmware.html#over-stock` and `hardware.html#unlocked`, which link to the full comparison in `comparison.html`. The price, firmware, and capability tables are present in the HTML. JavaScript switches comparison groups and calculates shared-row rankings. The comparison uses FiiO ECHO 2.0.0: M3U playlists, song favorites, EQ, gain, five DAC filters, Bluetooth and USB DAC mode are existing stock capabilities. Hardware copy describes firmware controls and mechanisms; it makes no measured battery-life, analog-quality or output-power improvement claim.

Homepage screenshots reuse the original simulator PNGs under `site/assets/screens/`. The surrounding player shapes are CSS illustrations. All visible features describe completed capabilities; plans and unfinished integrations stay outside the public feature copy.

When changing a product or affiliate link, update every placement and verify the destination model and capacity.


## Awin purchase links

Doublesharp’s Awin publisher ID is `3152908`. Network account activation and advertiser-program approval are separate. Add a retailer to the public site after its program appears under Joined and the product’s eligibility is confirmed.

| Advertiser | Awin merchant ID | Snowsky ECHO destination |
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

The supplied product screenshots identify all three as the Snowsky ECHO. Keep the short promotion links exactly as generated, label capacity where shown, and do not describe the sellers as FiiO’s official store. Product photos, sale prices and commission percentages are not copied into the website.

## Companion screenshots and availability

The Companion page, screenshots, and social card are public. Shared navigation, the home-page promotion, and the sitemap link to it. The desktop screenshots are product assets under `site/assets/companion/`; they use a sample music library and synthetic player folders, never the owner’s player. Desktop images use a window frame rather than the firmware’s player outline.

```sh
node tools/site_companion_screenshots.mjs --source-root /path/to/gnarlyr-companion
```

Without `--source-root`, the script uses `../gnarlyr-companion` beside this checkout.

Requires Node 24+, ffmpeg and the companion’s installed dependencies. `tools/site_companion_screenshots.json` configures the viewport and thirteen captured views. The script rebuilds the app, launches its actual Electron interface with isolated Scratch user data, drives the GUI and confirms that sync preview changes no files, settings stay unchanged and no firmware is staged. All thirteen views render before public images are replaced. `--no-build` explicitly reuses an existing build; `--config` selects a different capture configuration. Dependencies come from the companion checkout’s existing installation. The capture checks for its estimated 1 GiB of working space before building and while creating sample albums; normal runs also follow the shared resource preflight. Use `--output` with a path under the project's Scratch directory to review the captures before replacing the site assets. The scenes include a Spanish interface and the player-library preparation review. The header uses the Sunset Disc, and the Themes fixture is built with the app’s hash-pinned theme tooling.

The desktop app is developed in the private `gnarlyr-desktop` repository. The public page previews the beta. Add versioned [release assets](https://github.com/0xdoublesharp/gnarlyr/releases) when installers are ready. See the [desktop publishing requirements](../docs/downloads.md#desktop) for signing and distribution terms.

The capture scenes show library browsing, reviewed music copies, native mixtapes, C60/C90 cassette editing, player-library preparation, and themes. Music copying preserves existing files and keeps verified files when canceled. Library preparation and ejection have their own controls. App and player languages are independent. The app follows the first supported system language until the user chooses one, and uses English for untranslated app labels. Firmware-default selection follows the reference image.

The bundled engine uses library and scan format 2. Verified internal input is included when the library is written to the card. The companion builds and previews version 2 theme packs, detects the active theme through `theme.bin`, and checks installed bytes and keeps a local backup when installing or removing a pack.

The browser language builder and desktop importer support the firmware’s initial segmented version 1 pack format. Checks cover file size, section checksums, text/glyph validity and peak player-memory use. Export the catalogue from the matching firmware source and run `test_language_browser.py` against that source; `SITE_SOURCE_ROOT` selects another checkout.


## Analytics

Public pages, including `/comparison.html` and the translated homepages, send pageviews and interaction events to the shared Umami service on production HTTPS at `gnarlyr.com`.

- `download`, `retailer_click`, and `outbound_link` count clicks on files, retailer offers, and external resources.
- `preview_tab`, `comparison_filter`, `command_copy`, `lightbox_open`, and `language_switch` record which controls visitors use.
- `language_builder` records when a visitor opens an official release pack in the editor or downloads a built pack. It sends the action and a recognized language code. Other valid codes are grouped as `custom`.

Do Not Track, Global Privacy Control, the `umami.disabled` browser preference, and unavailable local storage suppress collection. The sender omits cookies, URL query strings and fragments, retailer affiliate data, clipboard contents, and translation text. Referrers contain only their origin. Session recording is disabled.

Run `node --test tests/test_site_analytics.mjs` to check the routes, event payloads, and privacy controls.

## Album artwork in screenshots

Both capture tools feature **Bodhi McBoatface — Priceless 2023**, using the owner-selected embedded artwork in `site/assets/artwork/priceless-2023.jpg`. The catalog records that choice and pins the original bytes by SHA-256. Other albums are demonstration metadata paired with royalty-free photographs under the [Unsplash License](https://unsplash.com/license). Source and license links are kept with each entry in `tools/site_album_art.json`. `tools/site_album_art.py` reads local product artwork or caches downloaded JPEGs on Scratch and refuses changed bytes. The capture tools embed these images in generated FLACs; no third-party audio is downloaded or distributed. Keep generated music and temporary captures on Scratch; product artwork and finished screenshots belong in `site/assets/`.
