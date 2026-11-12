# Gnarlyr theme pack format

A `.gtheme` is data: metadata, a complete RGB565 palette, a bounded image directory and optional RGB565 pixels. It contains no executable content, scripts, fonts, shaders, bytecode, addresses, loaders or firmware patches. Copying a pack never flashes firmware. Version 1 is independent of the language catalogue and firmware release number; future readers must preserve its role and slot numbers or explicitly reject its format version.

## Making and installing a pack

Put `theme.json` or `theme.toml` and opaque PNG assets in one source folder. Build with Python 3 and Pillow:

```sh
python3 tools/theme_pack.py examples/themes/pocket-scope --output /Volumes/Scratch/SnowskyEcho/pocket-scope.gtheme
python3 tools/theme_pack.py --check /Volumes/Scratch/SnowskyEcho/pocket-scope.gtheme
```

Copy the resulting file into `.gnarlyr/themes` on the SD card, or internal storage when using no card. Eject normally, then open Settings → Theme. The five built-ins precede installed packs. Highlight previews; Play saves; Back cancels. The simulator uses the same files and validator: with `--card`, put packs in that card directory's `.gnarlyr/themes`; without a card, put them in `<system-dir>/themes`.

A pack's nonzero UUID is its identity. Keep it when publishing an update and increase the numeric `major.minor.patch` version. Each part is 0–65535, with no leading zeroes except zero itself. Giving `--output` an existing directory writes `<uuid>.gtheme`, so copying an update over that filename replaces the old file. If several files have one UUID, the player lists the highest version; equal versions use the lexicographically smallest filename. Selection follows the UUID across renames and updates. A damaged update is listed as Damaged; another intact version with the same ID can still be selected.

The list holds at most 12 installed entries and examines at most the first 128 directory entries. Keep that folder small. Names beginning with a dot, hidden files, subdirectories, and names over 63 UTF-8 bytes are ignored. Filenames must end in lowercase `.gtheme` and cannot contain path separators or control bytes. Installed names sort by their UTF-8 byte order after validation.

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
  "dither": false,
  "palette": {},
  "assets": [
    {"slot": "home", "file": "home.png", "orientation": "landscape"},
    {"slot": "list", "file": "list.png", "orientation": "any"},
    {"slot": "icon_music", "file": "music.png", "text_size": "small"}
  ]
}
```

The palette object must supply all 20 roles below as `#RRGGBB`; `{}` above is an abbreviation. Base is `graphite`, `neon`, `sunset`, `glass` or `classic`. Name, author, version and licence are required; homepage and gallery may be empty or omitted. Licence may be an SPDX identifier/expression or a short free-text licence identifier. Include fuller licence terms with the source distribution. URLs are informational metadata, must start with `https://`, and are never opened or fetched by the player. The gallery URL is optional and grants no access control.

PNG paths must remain inside the source folder, including after resolving symlinks. Transparency is refused: composite it onto the palette background first. Images keep their original dimensions; the builder refuses an incorrectly sized image instead of silently stretching it. RGB565 conversion truncates red/blue to five bits and green to six. `dither: true` applies a fixed 4×4 ordered dither before conversion. Assets are optional, including the entire directory. A missing orientation/size/slot uses the base drawing, without scaling another orientation's background.

## Binary layout

All integers are unsigned little-endian. Total size is 512 bytes to 4,194,304 bytes, including an optional signature. The fixed header is 512 bytes:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | `DSTP` |
| 4 | 2 | Format version, 1 |
| 6 | 2 | Header size, 512 |
| 8 | 4 | Exact total file length |
| 12 | 4 | IEEE CRC-32 of bytes 0–11 followed by bytes 16 through the end of the image payload; excludes this field and the signature block |
| 16 | 1 | Base theme: 0 Graphite, 1 Neon Noir, 2 Sunset, 3 Sea Glass, 4 Classic |
| 17 | 1 | Bit 0 horizon, bit 1 header rule; other bits zero |
| 18 | 1 | Palette count, 20 |
| 19 | 1 | Asset count, 0–32 |
| 20 | 16 | UUID bytes in RFC 4122/network byte order; nonzero |
| 36 | 32 | Name |
| 68 | 32 | Author |
| 100 | 32 | Pack version, numeric `major.minor.patch` |
| 132 | 64 | Licence identifier |
| 196 | 128 | Homepage URL, optional |
| 324 | 128 | Gallery/store URL, optional |
| 452 | 40 | 20 RGB565 colours in the role order below |
| 492 | 2 | Signature block size: 0 or 88 |
| 494 | 18 | Zero; reserved for a future format version |

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

The directory starts at offset 512, with 16 bytes per asset:

| Relative offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | Slot |
| 2 | 1 | Orientation: 0 landscape, 1 portrait, 2 either |
| 3 | 1 | Text size: 0 small, 1 medium, 2 large, 255 all |
| 4 | 2 | Width |
| 6 | 2 | Height |
| 8 | 4 | Absolute pixel offset |
| 12 | 4 | Pixel byte count, exactly width × height × 2 |

Pixels are opaque, top to bottom, left to right, RGB565 little-endian. No indexed or compressed encoding exists in version 1. Payloads must be contiguous, in directory order, immediately after the directory. The final payload must end exactly before the signature, or EOF when unsigned. Duplicate `(slot, orientation, text_size)` tuples, overlaps, gaps, trailing payload, integer-overflow attempts and truncated reads are rejected. An exact orientation takes priority over either; then an exact text size takes priority over all.

| Slot | Manifest name | Dimensions and variants |
| --- | --- | --- |
| 1 | `home` | 480×222 landscape or 222×480 portrait; text size all |
| 2 | `now_playing` | Same full-screen dimensions and variants |
| 3 | `list` | Same full-screen dimensions, or a repeating tile of 1–64 pixels on each side; text size all; tiles may use either orientation |
| 4 | `splash` | Same full-screen dimensions and variants; drawn after `board_power_hold`, in the app splash only |
| 5 | `cassette_label` | 160×48; named reservation for the cassette view, with orientation variants; text size all |
| 256 + icon ID | `icon_<name>` | Native icon grid: large 20×20, small 14×14, narrow 9×14; all three text sizes use these fixed boxes; exact size variants may be supplied |

Icon IDs are the explicit ordinal values of [`../firmware/src/ui/include/ui/icons.h`](../firmware/src/ui/include/ui/icons.h), beginning with Music at 1. Existing IDs must never be reordered or reused within format version 1. Large icons run Music through Search. Small icons run Play through Shuffle Small. Bolt, Bluetooth Small, Lock and Song Small are narrow. `NONE` and `COUNT` are not assets. The builder accepts the corresponding lowercase names, such as `icon_music`, `icon_now_playing`, `icon_play` and `icon_bluetooth_small`.

Home item icons and status icons use these replacements when their destination box fits the native image. A larger box places the image at its top left; a smaller box retains the vector icon. Other screen icons keep their built-in drawing. Status battery outline/fill and the bolt's charging halo remain palette-driven. The cassette slot has no caller in this branch; the cassette view can use the shared theme art hook without changing the format.

Background coordinates are screen coordinates, including during clipped redraws and slides. Home paints the list portion with its Home background; the cover-art side panel retains its cover, caption and base drawing. Now Playing paints its content behind album art, labels and progress. Lists paint their content behind selection and text. The title/status band keeps its solid palette background. Splash art appears beneath the normal splash text. Keep imagery under text quiet: the tool warns about palette contrast, but does not assess arbitrary artwork.

## Validation and rendering

The player validates the entire file, including CRC and every asset bound, before changing a palette or retaining its file handle. A failed candidate never partially replaces an active pack. Invalid files appear as Damaged and Play leaves the picker open. Low palette contrast is a warning, not a rejection: text/background, secondary text/background, selection text/selection and header text/header are tested against 4.5:1 using the same integer quadratic luminance approximation in tool and firmware. This is a readability warning rather than a WCAG conformance claim.

The loader, parser, catalogue, selection writer and Theme screen live in the demand-loaded listening module. Only palette application and bounded blitting stay resident. Active metadata and descriptors are copied into resident caller-owned state; no pointers into evictable BSS survive. The catalogue shares the existing listening/lyrics/smart-list work union, and rebuilds after module eviction or a change of owner. CRC/signature reads service audio between 512-byte chunks.

Rendering reads only the clipped pixels into the existing strip canvas. Full-width strips use one contiguous read; partial rectangles read row segments. A fixed 64-byte cache holds one small tile or icon, never a full-screen image. A failed image read disables pack artwork and falls back to ordinary drawing. Selection lives on internal storage in `/.gnarlyr/theme.bin`: two 512-byte `DSTS` version-1 store banks with generation, payload hash and verified read-back. Its 17-byte payload is the UUID plus built-in base index. Choosing a built-in stores a zero UUID. Settings v15 is unchanged. An absent/damaged selected pack at startup uses Neon Noir, the built-in default; removing a card cannot prevent startup. An unavailable listening module leaves built-in drawing usable.

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
python3 tools/theme_pack.py examples/themes/pocket-scope --output /Volumes/Scratch/SnowskyEcho/signed.gtheme --sign-key /path/to/private-seed --public-key-out /Volumes/Scratch/SnowskyEcho/public.key
python3 tools/theme_pack.py --check /Volumes/Scratch/SnowskyEcho/signed.gtheme --verify-key /Volumes/Scratch/SnowskyEcho/public.key
```

Without a matching public key, `--check` reports `unknown-key` while checking all ordinary data. With a matching key it verifies the signature and fails on a forgery. Firmware verification uses pinned Monocypher 4.0.2 in the cold module, never resident code. `theme_trusted_keys.h` holds a build-time list of at most four key IDs/public keys; it ships empty until the owner provisions gallery keys. Thus this build does not award Verified badges. Adding keys requires a fresh size-budget check; unused verification code is discarded when there are no keys.

A valid signature by a trusted key earns Verified in the list. Unsigned, unknown-key and invalid-signature packs remain usable without that badge. A malformed signature envelope fails structural validation. Verified means attribution to a trusted signing key, not ownership, safety certification, payment or permission to run code. There is no DRM, entitlement lookup, network check or lock. A future gallery/editor and trust-key publication/revocation policy are separate work.
