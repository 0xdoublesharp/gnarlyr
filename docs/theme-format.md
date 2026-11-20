# Gnarlyr theme pack format

A `.gtheme` is data: metadata, a complete RGB565 palette, a bounded image directory, an optional style table and compressed images. It contains no executable content, scripts, fonts, shaders, bytecode, addresses, loaders or firmware patches. Copying a pack never flashes firmware. This page describes format version 2. Version 1 files are refused; rebuild them from their source folder. The format is independent of the language catalogue and the firmware release number.

## What the firmware draws

The current firmware applies a pack's palette (all 20 roles), its base theme, the horizon and header-rule flags, and these images: `home` behind Home and the Theme list, `now_playing` behind Now Playing (indexed only), `list` behind list screens, `splash` in the start-up splash, and the icons in the status bar and on Home. It checks and keeps the rest of a valid pack without using it: the `cassette_label` image and every style table entry. The builder and `--check` list those in their JSON `warnings` as not yet drawn or not yet applied, so a pack can carry them now and gain them when the firmware does.

## Making and installing a pack

Put `theme.json` or `theme.toml` and PNG assets in one source folder. Build with Python 3 and Pillow:

```sh
python3 firmware/tools/theme_pack.py examples/themes/pocket-scope --output /Volumes/Scratch/SnowskyEcho/pocket-scope.gtheme --preview /Volumes/Scratch/SnowskyEcho/pocket-scope-preview
python3 firmware/tools/theme_pack.py --check /Volumes/Scratch/SnowskyEcho/pocket-scope.gtheme
```

The build prints each image's codec, size, compression ratio and PSNR against plain RGB565 conversion, and warns below 30 dB. `--preview DIR` writes every image as the player draws it, transparent parts over a grey checkerboard. `--check` runs the same complete validation the player runs when it selects a pack.

Copy the resulting file into `.gnarlyr/themes` on the SD card, or internal storage when using no card. Eject normally, then open Settings → Theme. The five built-ins precede installed packs. Highlight previews; Play saves; Back cancels. The simulator uses the same files and validator: with `--card`, put packs in that card directory's `.gnarlyr/themes`; without a card, put them in `<system-dir>/themes`.

A pack's nonzero UUID is its identity. Keep it when publishing an update and increase the numeric `major.minor.patch` version. Each part is 0–65535, with no leading zeroes except zero itself. Giving `--output` an existing directory writes `<uuid>.gtheme`, so copying an update over that filename replaces the old file. If several files have one UUID, the player lists the highest version; equal versions use the lexicographically smallest filename. Selection follows the UUID across renames and updates.

The list holds at most 12 installed entries and examines at most the first 128 directory entries. Keep that folder small. Names beginning with a dot, hidden files, subdirectories, and names over 63 UTF-8 bytes are ignored. Filenames must end in lowercase `.gtheme` and cannot contain path separators or control bytes. Installed names sort by their UTF-8 byte order.

## Manifest

The complete examples are in [`../examples/themes`](../examples/themes). A JSON manifest has these fields; TOML uses the same names, a `[palette]` table and `[[assets]]` records:

```json
{
  "id": "c579606c-e2cc-45f6-87a8-3551008c47e2",
  "name": "Pocket Scope",
  "author": "Original pixel workshop",
  "version": "1.0.0",
  "licence": "CC0-1.0",
  "homepage": "https://example.org/pocket-scope",
  "gallery": "",
  "base": "graphite",
  "horizon": false,
  "header_rule": true,
  "codec": "indexed",
  "dither": "none",
  "palette": {},
  "assets": [
    {"slot": "home", "file": "home.png", "orientation": "landscape", "codec": "lossless"},
    {"slot": "now_playing", "file": "photo.png", "orientation": "landscape", "dither": "fs"},
    {"slot": "list", "file": "list.png", "orientation": "any"},
    {"slot": "icon_music", "file": "music.png", "text_size": "small"}
  ],
  "style": [{"kind": "scene", "target": "horizon", "value": 60}]
}
```

The palette object must supply all 20 roles below as `#RRGGBB`; `{}` above is an abbreviation. Base is `graphite`, `neon`, `sunset`, `glass` or `classic`. Name, author, version and licence are required; homepage and gallery may be empty or omitted. Licence may be an SPDX identifier/expression or a short free-text licence identifier. Include fuller licence terms with the source distribution. URLs are informational metadata, must start with `https://`, and are never opened or fetched by the player. The gallery URL is optional and grants no access control.

PNG paths must remain inside the source folder, including after resolving symlinks. Images keep their original dimensions; the builder refuses an incorrectly sized image instead of silently stretching it. Only icons may be transparent; other images must be opaque (composite them onto a background first). A missing orientation, size or slot uses the base drawing, without scaling another orientation's background.

Each image chooses its encoding with `codec`, falling back to the manifest's `codec` and then to `indexed`:

- `indexed` stores up to 256 colours per image. An image with at most 256 colour and alpha pairs after rounding to RGB565 and 4-bit alpha is stored exactly. Otherwise it is quantised: median cut for opaque images, fast octree for transparent icons. It is the fastest to draw and the smallest for flat art.
- `lossless` stores every RGB565 pixel exactly. Use it for photographs whose quantisation is visible. It draws more slowly; see Rendering cost. It is refused in the `now_playing` slot and allowed in every other slot.

Now Playing redraws its title, time and progress while music plays, so its background costs processor time for as long as the screen is on. The builder refuses a lossless `now_playing` image, names the slot and suggests `indexed`; `--check` refuses a pack that has one. It builds an indexed `now_playing` image with a warning in the JSON `warnings` list that the image raises processor load and shortens battery life while the screen is on. The player treats a lossless `now_playing` image in an installed pack as absent: Now Playing uses the theme's colours and the rest of the pack still applies.

`dither` (per image or for the whole manifest) is `none`, `ordered` (the 4×4 ordered dither before RGB565 conversion) or `fs` (Floyd–Steinberg during quantisation, indexed only). RGB565 conversion truncates red and blue to five bits and green to six; alpha is rounded to four bits (0 transparent, 15 opaque).

`style` lists optional scene and screen parameters (Style table, below). Each entry names a kind, a target and a value.

## Binary layout

All integers are unsigned little-endian. Total size is 512 bytes to 2,097,152 bytes, including an optional signature. File order: the header, the directory (asset count × 24 bytes), the style table, the payloads contiguous in directory order, then the optional signature. The fixed header is 512 bytes:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | `DSTP` |
| 4 | 2 | Format version, 2 |
| 6 | 2 | Header size, 512 |
| 8 | 4 | Exact total file length |
| 12 | 4 | IEEE CRC-32 of bytes 0–11 followed by bytes 16 through the end of the last payload; excludes this field and the signature block |
| 16 | 1 | Base theme: 0 Graphite, 1 Neon Noir, 2 Sunset, 3 Sea Glass, 4 Classic |
| 17 | 1 | Bit 0 horizon, bit 1 header rule; other bits zero |
| 18 | 1 | Palette count, 20 |
| 19 | 1 | Asset count, 0–64 |
| 20 | 16 | UUID bytes in RFC 4122/network byte order; nonzero |
| 36 | 32 | Name |
| 68 | 32 | Author |
| 100 | 32 | Pack version, numeric `major.minor.patch` |
| 132 | 64 | Licence identifier |
| 196 | 128 | Homepage URL, optional |
| 324 | 128 | Gallery/store URL, optional |
| 452 | 40 | 20 RGB565 colours in the role order below |
| 492 | 2 | Signature block size: 0 or 88 |
| 494 | 2 | Style table length in bytes: a multiple of 4, at most 256 |
| 496 | 16 | Zero |

Text fields are valid UTF-8 terminated by a zero with all remaining bytes zero. Their capacities include that terminator: name/author/version at most 31 bytes, licence at most 63, URLs at most 127. C0 controls and DEL are forbidden. Unknown versions, flags, slots, variant values, nonzero reserved bytes and malformed metadata fail validation.

Palette roles, in order:

1. `background`
2. `text`
3. `text_secondary`
4. `text_tertiary`
5. `selection_background`
6. `selection_top`
7. `selection_shine`
8. `selection_text`
9. `header_background`
10. `header_top`
11. `header_text`
12. `divider`
13. `scrollbar_thumb`
14. `danger`
15. `success`
16. `chevron`
17. `accent`
18. `accent_alt`
19. `backdrop_top`
20. `backdrop_bottom`

These include the colours used by status, selection, charge and USB art. Charge/USB use their existing palette-driven drawing; there are no independent hidden RGB constants to supply in a pack. Accent Color continues to affect the Graphite built-in; an external pack owns its complete palette.

## Images

The directory starts at offset 512, with 24 bytes per asset:

| Relative offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | Slot |
| 2 | 1 | Orientation: 0 landscape, 1 portrait, 2 either |
| 3 | 1 | Text size: 0 small, 1 medium, 2 large, 255 all |
| 4 | 2 | Width |
| 6 | 2 | Height |
| 8 | 4 | Absolute payload offset |
| 12 | 4 | Payload bytes |
| 16 | 1 | Encoding: 1 indexed, 2 lossless |
| 17 | 1 | Indexed: palette entries − 1 (0–255). Lossless: 0 |
| 18 | 1 | Indexed: alpha entries (0 to the entry count, at most 255). Lossless: 1 if the rows may contain transparency, else 0. Must be 0 in opaque slots |
| 19 | 1 | Flags: bit 0 tile (list background only); other bits zero |
| 20 | 1 | Anchor; 0 for every slot in this version |
| 21 | 1 | Horizontal offset; 0 for every slot in this version |
| 22 | 1 | Vertical offset; 0 for every slot in this version |
| 23 | 1 | Frames; 1 for every slot in this version |

The first payload starts right after the style table, the next one right after it, and the last one ends exactly before the signature, or at the end of the file when unsigned. Duplicate `(slot, orientation, text_size)` tuples, overlaps, gaps, trailing payload, integer-overflow attempts and truncated reads are rejected. An exact orientation takes priority over either; then an exact text size takes priority over all.

| Slot | Manifest name | Dimensions and variants | Transparency |
| --- | --- | --- | --- |
| 1 | `home` | 480×222 landscape or 222×480 portrait; text size all | No |
| 2 | `now_playing` | Same full-screen dimensions and variants; indexed only | No |
| 3 | `list` | Same full-screen dimensions, or a repeating tile of 1–64 pixels on each side with the tile flag; text size all; tiles may use either orientation | No |
| 4 | `splash` | Same full-screen dimensions and variants; drawn after `board_power_hold`, in the app splash only | No |
| 5 | `cassette_label` | 160×48; named reservation for the cassette view, with orientation variants; text size all | No |
| 256 + icon ID | `icon_<name>` | Native icon grid: large 20×20, small 14×14, narrow 9×14; all three text sizes use these fixed boxes; exact size variants may be supplied | Yes |

Icon IDs are the explicit ordinal values of [`../firmware/src/ui/include/ui/icons.h`](../firmware/src/ui/include/ui/icons.h), beginning with Music at 1. Existing IDs must never be reordered or reused. Large icons run Music through Search. Small icons run Play through Shuffle Small. Bolt, Bluetooth Small, Lock and Song Small are narrow. `NONE` and `COUNT` are not assets. The builder accepts the corresponding lowercase names, such as `icon_music`, `icon_now_playing`, `icon_play` and `icon_bluetooth_small`.

Home item icons and status icons use these replacements when their destination box fits the native image. A larger box places the image at its top left; a smaller box retains the vector icon. A transparent icon is drawn over what is already there. Other screen icons keep their built-in drawing. Status battery outline/fill and the bolt's charging halo remain palette-driven. The cassette slot has no caller in this branch.

Background coordinates are screen coordinates, including during clipped redraws and slides. Home paints the list portion with its Home background; the cover-art side panel retains its cover, caption and base drawing. Now Playing paints its content behind album art, labels and progress. Lists paint their content behind selection and text. The title/status band keeps its solid palette background. Splash art appears beneath the normal splash text. Keep imagery under text quiet: the tool warns about palette contrast, but does not assess arbitrary artwork.

## Image encodings

Every payload has the same frame:

1. A 4-byte header: encoding, palette entries − 1 (0 for lossless), alpha entries or the lossless transparency flag, frames. It must equal descriptor bytes 16, 17, 18 and 23.
2. Indexed only: the palette, entries × RGB565, then the alpha nibbles, ⌈alpha entries ÷ 2⌉ bytes, one 4-bit value per entry, low nibble first. Entries below the alpha count carry these values (0–14; 0 is transparent); the remaining entries are opaque. An unused high nibble is zero.
3. The row table: (height + 1) 32-bit offsets from the start of the payload. Entry 0 points just past the table and the last entry equals the payload size. Offsets strictly increase, and every row is at most 1,024 bytes.
4. The rows. Each row decodes on its own to exactly `width` pixels and uses every one of its bytes.

**Indexed rows** are packets. A byte `p` below `0x80` is followed by p + 1 palette indices (a literal); a byte from `0x80` is followed by one index repeated (p & 0x7F) + 1 times. Every index is below the entry count. A 480-pixel row is at most 484 bytes.

**Lossless rows** start from black and keep the previous colour:

| Packet | Pixels | Following bytes | Meaning |
| --- | --- | --- | --- |
| `0x00`–`0x3F` | (p & 0x3F) + 1 | none | Repeat the previous colour |
| `0x40`–`0x7F` | (p & 0x3F) + 1 | one per pixel | Deltas from the previous colour. For a byte `d`: green changes by (d >> 4) − 8; red by (d >> 5) − 4 + ((d >> 2) & 3) − 2; blue by (d >> 5) − 4 + (d & 3) − 2. Each channel wraps within its 5 or 6 bits |
| `0x80`–`0xBF` | (p & 0x3F) + 1 | two per pixel | RGB565 colours; the last one becomes the previous colour |
| `0xC0`–`0xDF` | (p & 0x1F) + 1 | none | Transparent pixels: leave the screen unchanged |
| `0xE1`–`0xEE` | none | the next packet | Alpha (p − 0xE0, 1–14) for the next repeat, delta or literal packet only |

`0xE0` and `0xEF`–`0xFF` are reserved. Transparent pixels and alpha prefixes are allowed only when the descriptor's transparency flag is set, which only icons may do. A prefix must be followed by a repeat, delta or literal packet in the same row. An opaque 480-pixel row is at most 968 bytes; the builder refuses a transparent row that would exceed 1,024 bytes and suggests the indexed codec.

## Style table

The style table follows the directory. Each 4-byte entry is `u8 kind, u8 target, u16 value`; each `(kind, target)` appears once. Unknown kinds or targets and out-of-range values are rejected. Firmware validates the table but no screen uses it yet, and the builder warns that each entry is not yet applied; it reserves these parameters for the planned visualizer stages and Now Playing options:

| Kind | Manifest name | Targets and ranges |
| --- | --- | --- |
| 1 | `scene` | 0 `horizon` 40–75 (% of the stage), 1 `sun_radius` 15–35 (%), 2 `slats` 0–8, 3 `floor_lines` 3–10, 4 `rays` 0–16, 5 `pulse` 0–1 |
| 3 | `wait_frame_ms` | 0 `time` 40–500 |
| 4 | `now_playing` | 0 `align` 0 left, 1 centre; 1 `progress_height` 4, 6 or 8 pixels |

Kind 2 (per-screen colour overrides) is reserved and rejected.

## Validation and rendering

The player checks a pack at two depths:

- **Structure**, when the catalogue is scanned: the header, every descriptor bound, the style table and the payload layout. A file that fails is listed as Damaged.
- **Complete**, when a pack is highlighted (preview), selected, and at start-up for the selected pack only: also the CRC, the signature envelope, every payload header and alpha nibble, the row table and every row's packets, decoded without output. The CRC pass and the row pass each read the file in pieces of at most 1,024 bytes and service audio between them.

A failed candidate never partially replaces an active pack. A file that fails its complete check is remembered as Damaged while the views module stays loaded (until the next start or USB session), listed as Damaged, and the next intact version with the same UUID is tried; at start-up up to four versions are tried, highest first. If none is intact the player starts with Neon Noir, the built-in default. Low palette contrast is a warning, not a rejection: text/background, secondary text/background, selection text/selection and header text/header are tested against 4.5:1 using the same integer quadratic luminance approximation in tool and firmware. This is a readability warning rather than a WCAG conformance claim.

The loader, parser, catalogue, selection writer and Theme screen live in the views module (module 1), which keeps the catalogue's work buffer in its own BSS; the catalogue rebuilds after the module is evicted. Drawing lives in the extension module (module 8), which stays loaded beside every UI module, so art also draws when no UI module is loaded. Only palette application and the drawing hook stay resident. Active metadata and the asset keys (slot, orientation, text size) are copied into resident caller-owned state; no pointers into evictable BSS survive.

Rendering decodes only the rows and columns of the current redraw rectangle into the existing strip canvas. Each draw call reads the asset's descriptor and palette, then reads the row table in batches of 32 rows and the rows into a 4,608-byte window on the stack, starting on a 512-byte boundary so the card transfers it directly. Because the card can change after validation, each draw checks again what it reads: the descriptor must still match the slot, orientation and text size accepted at selection, fit the slot's dimensions and opacity rule, use a valid encoding (indexed only for `now_playing`) and lie inside the file; the payload header must match the descriptor; every packet must stay inside the row's bytes and width, a row must end exactly at its last pixel, and every palette index drawn must be below the palette's entry count. No full-screen image buffer is allocated. A failed read or any of these checks failing disables pack artwork for the session and redraws the whole screen with ordinary drawing. Selection lives on internal storage in `/.gnarlyr/theme.bin`: two 512-byte `DSTS` version-1 store banks with generation, payload hash and verified read-back. Its 17-byte payload is the UUID plus built-in base index. Choosing a built-in stores a zero UUID. Removing a card cannot prevent startup. An unavailable views module leaves built-in drawing usable, and without the extension module every screen keeps its palette drawing.

### Rendering cost

Measured by `firmware/tools/art_codec_bench.py` (Cortex-M3 at `-Os`, emulated instructions per output pixel; instructions, not cycles). On a study corpus of photographs, indexed rows cost 11–15 instructions per pixel and lossless rows 32–37; for flat and UI-like art, 4–8 and 5–16. A part of a row costs more per visible pixel, because a row decodes from its start: for columns 300–400 of a photograph, 35–56 indexed and 146–173 lossless. Prefer `indexed` behind text that scrolls.

Behind Now Playing, in the emulator with a 96 kHz FLAC of 512-sample blocks and a scrolling title (`firmware/tests/emulator/scenarios/test_36_theme_art_feed.py`), core 0 is 12.7 % busy with a built-in theme and 17.4 % with an indexed full-screen photograph. A lossless photograph measured 23.7 %, which is why that slot refuses it. These are emulator figures, not battery measurements on the player.

## Optional signatures and trust

An unsigned pack has signature size zero. A signed pack ends with this detached block:

| Relative offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | `DSSG` |
| 4 | 1 | Algorithm 1: Ed25519ph, RFC 8032, empty context |
| 5 | 3 | Zero |
| 8 | 16 | Key ID: first 16 bytes of SHA-256 of the raw 32-byte Ed25519 public key |
| 24 | 64 | Signature |

Ed25519ph signs every preceding byte, including metadata, UUID, palette, descriptors, pixels, total length and CRC. Compute SHA-512 incrementally over that prefix and apply Ed25519ph with empty context. This is Ed25519ph, not plain Ed25519 over a SHA-512 digest. The detached block is stored in the same file to keep copying atomic at the file level; the pack never contains a private key or a self-asserted trusted public key.

Signing/checking also require libsodium, loaded through Python's standard `ctypes` (`brew install libsodium` on macOS). A private-key input is a 32-byte seed, raw or 64 hex digits. Public-key inputs use the same raw/hex lengths:

```sh
python3 firmware/tools/theme_pack.py examples/themes/pocket-scope --output /Volumes/Scratch/SnowskyEcho/signed.gtheme --sign-key /path/to/private-seed --public-key-out /Volumes/Scratch/SnowskyEcho/public.key
python3 firmware/tools/theme_pack.py --check /Volumes/Scratch/SnowskyEcho/signed.gtheme --verify-key /Volumes/Scratch/SnowskyEcho/public.key
```

Without a matching public key, `--check` reports `unknown-key` while checking all ordinary data. With a matching key it verifies the signature and fails on a forgery. Firmware verification uses pinned Monocypher 4.0.2 in the cold module, never resident code. `theme_trusted_keys.h` holds a build-time list of at most four key IDs/public keys; it ships empty until the owner provisions gallery keys. Thus this build does not award Verified badges. Adding keys requires a fresh size-budget check; unused verification code is discarded when there are no keys.

A valid signature by a trusted key earns Verified in the list once the pack has had its complete check (when highlighted or selected). Unsigned, unknown-key and invalid-signature packs remain usable without that badge. A malformed signature envelope fails structural validation. Verified means attribution to a trusted signing key, not ownership, safety certification, payment or permission to run code. There is no DRM, entitlement lookup, network check or lock. A future gallery/editor and trust-key publication/revocation policy are separate work.
