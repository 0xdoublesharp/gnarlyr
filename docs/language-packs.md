# Language packs

Open **Settings → Language** to choose English, German, Spanish, French,
Japanese, Portuguese, or Simplified Chinese. These seven languages ship in the
firmware image and are ready to use at every text size.

## Make or check a pack in a browser

Open [the language-pack builder](../site/languages.html). It runs locally, including when opened directly from a source checkout, and does not upload files. Download its English template, translate the `STR_NAME = text` entries, choose a language code such as `es` and enter the native language name, such as `Español`. Untranslated entries use English. Preserve numbered placeholders such as `{0}` and write a line break as `\n`.

The builder checks the catalogue, supported characters and the memory the pack needs on the player before offering a download. It can also inspect an existing `.pack` file. Both checks tell you if the pack needs `extra.font`. With the player connected over USB, use Gnarlyr Companion or run `python3 tools/echo_sync.py` from the `firmware` directory to create that file. The browser catalogue belongs to the matching source checkout. Use the USB tools below to check compatibility with the firmware actually installed on a developer player.

To install a custom pack, choose **Connect to computer** on the player. Create
`.gnarlyr/lang` on the player's internal drive and copy the downloaded file
there, for example `pl.pack`. Eject the player's drives, then choose the
language by its native name in **Settings → Language**. Beta.3 and earlier
look for external packs on the active music drive.

## Checked USB workflow

The macOS debug helper and its MCP server share the language operations. They identify the player's internal and music volumes and read the installed build's catalogue and font configuration from local Git history. Keep that build's commit available. Unknown or modified build identifiers are refused for managed installation and selection.

Choose Connect to computer on the player, then run from the source checkout:

```sh
python3 tools/echo_debug.py language-list
python3 tools/echo_debug.py language-inspect /path/to/es.pack
python3 tools/echo_debug.py language-install /path/to/es.pack --select
python3 tools/echo_debug.py language-select en
```

`language-install` checks the copy, ejects all ECHO drives, refreshes the language list and waits for the developer build to reconnect. With `--select`, it also selects the new language and verifies the saved language code. `language-select` uses the current language's setting label, so returning to English works after a language change. `--music-volume PATH` can select a music volume only when it belongs to the same identified USB player.

Both `language-install` and `language-select` also rebuild `.gnarlyr/fonts/extra.font` on internal storage for the language being selected (or the saved one), the installed packs and the library before the player refreshes, and report it under `fonts`: `ok`, the `action` (`written`, `unchanged`, `removed` or `absent`), the `style`, the number of `characters`, the file `size`, the fully covered `languages` and the characters that can't be drawn. A font failure (`fonts_failed`, `fonts_unavailable` without the tools' Python packages, `fonts_unsupported` for a build that reads no extra fonts) does not stop the pack install. The first build downloads about 40 MB of Noto Sans CJK fonts into the per-user cache (firmware `docs/architecture.md`, Languages, Extra fonts).

To inspect offline, use `language-inspect /path/to/es.pack --build COMMIT`. The commit must resolve in local Git history. No device is needed for that command.

The stdio server `python3 tools/echo_debug_mcp.py` exposes `echo_language_inspect`, `echo_language_list`, `echo_language_install` and `echo_language_select`. Their `pack`, `build`, `music_volume`, `select` and `timeout` arguments mirror the corresponding command options. Both interfaces return machine-readable results and errors from the same operations.

Before publishing a pack, the helper validates its complete contents against that catalogue, writes a temporary file, flushes it and checks the readback. Replacing a pack retains a backup. Only language-pack paths and `.gnarlyr/fonts/extra.font` on internal storage are writable through these operations; firmware and debug operations keep their existing write boundaries.

After a firmware catalogue change, `language-list` reports old packs under `incompatible_files`. Install the rebuilt pack under the same code to preserve the previous bytes as a backup and publish its compatible replacement. If a pack uses an unsupported format, it appears with the reason `format_mismatch`. Import your saved translation text into the builder for your firmware version and download a new `.pack` file.

Safe host ejection gives storage ownership back to the player and refreshes its available languages. Built-in languages remain available if an external pack is absent or incompatible. The player reads each screen's text from the chosen pack on internal storage, so removing the microSD card leaves the language in use. Keep replacement backups: file checks do not guarantee recovery of an external translation after a power loss during filesystem publication.

## Font and format limits

The firmware accepts version 1 packs ([formats](formats.md#language-packs-gnarlyrlangcodepack-and-the-image)). A pack is split into resident text and one part per screen group; the player holds the resident text and one group at a time, and those two together must fit 18,432 bytes (English needs about 8.5 KB). The file itself may be up to 65,535 bytes. A file damaged in any part is not offered in Settings → Language. Language codes contain at most five ASCII letters, digits or hyphens. Native names must fit in 27 UTF-8 bytes. Packs must preserve the ordered string catalogue and each entry's numbered placeholders. Every translated character must be drawable at all three text sizes; western European accented letters and the Russian and Ukrainian Cyrillic ranges are supported. The Japanese and Simplified Chinese subsets cover all characters used by the official packs. The picker holds up to 16 languages, with nine slots available for custom packs in the standard image. Releases also attach the six translated packs, `en.pack`, and `language-catalog.json`. The browser builder accepts characters covered by the `extra.font` recipe, including Polish and Korean, and marks packs that need that file. A character the image's fonts lack is accepted when the font recipe covers it (Chinese, Japanese and Korean among others); the pack is then marked as needing extra fonts, and the language tools write `extra.font` with those characters when they install or select it. Until that file covers the language, Settings → Language shows it as needing fonts. Arabic and Indic scripts require shaping and additional font work.

For the binary format and image-embedded languages, see [Languages in the architecture guide](../firmware/docs/architecture.md#languages). To regenerate the browser catalogue after changing language entries or font coverage:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 firmware/tools/lang_pack.py --export-catalogue site/language-catalog.js
PYTHONDONTWRITEBYTECODE=1 python3 firmware/tools/lang_pack.py --export-catalogue site/language-catalog.js --check
```

The exported catalogue is deterministic. Its focused tests check that the committed browser asset matches the current English catalogue and font configuration. Firmware pack generation remains unchanged.
