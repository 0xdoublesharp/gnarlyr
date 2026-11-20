import argparse
import ctypes
import ctypes.util
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import tomllib
import uuid
import zlib

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import art_codec

ROOT = Path(__file__).resolve().parents[2]
VERSION = 2
HEADER_BYTES = 512
DESCRIPTOR_BYTES = 24
MAX_BYTES = 2097152
MAX_ASSETS = 64
STYLE_MAX_BYTES = 256
SIGNATURE_BYTES = 88
TILE = 1
CODECS = {"indexed": art_codec.INDEXED, "lossless": art_codec.LOSSLESS}
STYLE_KINDS = {"scene": 1, "wait_frame_ms": 3, "now_playing": 4}
STYLE_TARGETS = {
    1: {"horizon": (0, 40, 75), "sun_radius": (1, 15, 35), "slats": (2, 0, 8),
        "floor_lines": (3, 3, 10), "rays": (4, 0, 16), "pulse": (5, 0, 1)},
    3: {"time": (0, 40, 500)},
    4: {"align": (1 - 1, 0, 1), "progress_height": (1, 4, 8)},
}
ROLES = ("background", "text", "text_secondary", "text_tertiary", "selection_background",
         "selection_top", "selection_shine", "selection_text", "header_background",
         "header_top", "header_text", "divider", "scrollbar_thumb", "danger", "success",
         "chevron", "accent", "accent_alt", "backdrop_top", "backdrop_bottom")
BASES = ("graphite", "neon", "sunset", "glass", "classic")
SLOTS = {"home": 1, "now_playing": 2, "list": 3, "splash": 4, "cassette_label": 5}
ICON_NAMES = re.findall(r"^    ICON_([A-Z_]+)[,\n]", (
    ROOT / "firmware/src/ui/include/ui/icons.h").read_text(), re.MULTILINE)
SLOTS.update({"icon_" + name.lower(): 256 + index for index, name in enumerate(ICON_NAMES)
              if name not in ("NONE", "COUNT")})
ORIENTATIONS = {"landscape": 0, "portrait": 1, "any": 2}
TEXT_SIZES = {"small": 0, "medium": 1, "large": 2, "any": 255}


def rgb565(value):
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        raise ValueError("palette colours must be #RRGGBB")
    red, green, blue = bytes.fromhex(value[1:])
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def padded(value, size, required=True, url=False):
    if not isinstance(value, str) or (required and not value):
        raise ValueError("missing metadata")
    encoded = value.encode("utf-8")
    if len(encoded) >= size or any(ord(char) < 32 or ord(char) == 127 for char in value):
        raise ValueError(f"metadata must fit {size - 1} UTF-8 bytes without control characters")
    if url and value and (not value.startswith("https://") or len(value) <= 8):
        raise ValueError("URLs must be empty or start with https:// and name a host")
    return encoded.ljust(size, b"\0")


def text(data, start, size, required=True, url=False):
    field = data[start:start + size]
    value = field.split(b"\0", 1)[0].decode("utf-8", errors="strict")
    if field != padded(value, size, required, url):
        raise ValueError("metadata padding must be zero")
    return value


def icon_box(slot):
    name = ICON_NAMES[slot - 256]
    narrow = name in ("BOLT", "BLUETOOTH_SMALL", "LOCK", "SONG_SMALL")
    small = slot >= SLOTS["icon_play"]
    return (9 if narrow else 14, 14) if small else (20, 20)


def is_icon(slot):
    return 257 <= slot < 256 + ICON_NAMES.index("COUNT")


def dimensions(slot, orientation, size, width, height, flags=0):
    if orientation not in (0, 1, 2) or size not in (0, 1, 2, 255) or width < 1 or height < 1:
        raise ValueError("invalid asset variant or empty dimensions")
    if flags not in (0, TILE) or (flags and slot != 3):
        raise ValueError("only the list background may be a tile")
    if is_icon(slot):
        if (width, height) == icon_box(slot) and not flags:
            return
    elif slot == 5 and size == 255 and (width, height) == (160, 48):
        return
    elif slot in (1, 2, 3, 4) and size == 255:
        if flags == TILE and width <= 64 and height <= 64:
            return
        if not flags and orientation < 2 and (width, height) == ((480, 222) if orientation == 0 else (222, 480)):
            return
    raise ValueError(f"wrong dimensions for slot {slot}: {width}x{height}")


def style_entries(data):
    seen = set()
    for at in range(0, len(data), 4):
        kind, target, value = data[at], data[at + 1], struct.unpack_from("<H", data, at + 2)[0]
        limits = {limit[0]: limit[1:] for limit in STYLE_TARGETS.get(kind, {}).values()}
        if target not in limits or (kind, target) in seen:
            raise ValueError("unknown or repeated style entry")
        low, high = limits[target]
        if not low <= value <= high or (kind == 4 and target == 1 and value not in (4, 6, 8)):
            raise ValueError("style value out of range")
        seen.add((kind, target))


def style_table(entries):
    data = bytearray()
    for entry in entries or []:
        if set(entry) - {"kind", "target", "value"}:
            raise ValueError("unknown style fields")
        kind = STYLE_KINDS[entry["kind"]]
        target = STYLE_TARGETS[kind][entry["target"]][0]
        data += struct.pack("<BBH", kind, target, entry["value"])
    if len(data) > STYLE_MAX_BYTES:
        raise ValueError("at most 64 style entries")
    style_entries(bytes(data))
    return bytes(data)


def luminance(value):
    red = ((value >> 11) & 31) * 255 // 31
    green = ((value >> 5) & 63) * 255 // 63
    blue = (value & 31) * 255 // 31
    return (red * red * 2126 + green * green * 7152 + blue * blue * 722) // 10000


def contrast_warnings(colours):
    warnings = []
    for foreground, background in ((1, 0), (2, 0), (7, 4), (10, 8)):
        first, second = luminance(colours[foreground]) + 3251, luminance(colours[background]) + 3251
        if max(first, second) * 2 < min(first, second) * 9:
            warnings.append(f"Low contrast: {ROLES[foreground]} / {ROLES[background]} (below 4.5:1)")
    return warnings


def sodium():
    path = ctypes.util.find_library("sodium")
    if not path:
        raise ValueError("signing and verification require libsodium (brew install libsodium)")
    library = ctypes.CDLL(path)
    library.sodium_init.restype = ctypes.c_int
    if library.sodium_init() < 0:
        raise ValueError("libsodium initialization failed")
    return library


def key_bytes(value):
    raw = value.read_bytes() if isinstance(value, Path) else bytes(value)
    if len(raw.strip()) == 64:
        try:
            raw = bytes.fromhex(raw.decode().strip())
        except (ValueError, UnicodeError):
            pass
    if len(raw) != 32:
        raise ValueError("key must be 32 raw bytes or 64 hex digits")
    return raw


def key_pair(seed):
    library = sodium()
    public = ctypes.create_string_buffer(32)
    secret = ctypes.create_string_buffer(64)
    function = library.crypto_sign_seed_keypair
    function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    if function(public, secret, key_bytes(seed)) != 0:
        raise ValueError("cannot derive Ed25519 key")
    return public.raw, secret.raw


def public_key(seed):
    return key_pair(seed)[0]


def signature(data, key):
    library = sodium()
    library.crypto_sign_ed25519ph_statebytes.restype = ctypes.c_size_t
    state = ctypes.create_string_buffer(library.crypto_sign_ed25519ph_statebytes())
    library.crypto_sign_ed25519ph_init.argtypes = [ctypes.c_void_p]
    library.crypto_sign_ed25519ph_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulonglong]
    library.crypto_sign_ed25519ph_init(state)
    library.crypto_sign_ed25519ph_update(state, data, len(data))
    result = ctypes.create_string_buffer(64)
    function = library.crypto_sign_ed25519ph_final_create
    function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    if function(state, result, None, key) != 0:
        raise ValueError("Ed25519ph signing failed")
    return result.raw


def verify_signature(data, signed, key):
    library = sodium()
    library.crypto_sign_ed25519ph_statebytes.restype = ctypes.c_size_t
    state = ctypes.create_string_buffer(library.crypto_sign_ed25519ph_statebytes())
    library.crypto_sign_ed25519ph_init.argtypes = [ctypes.c_void_p]
    library.crypto_sign_ed25519ph_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulonglong]
    library.crypto_sign_ed25519ph_final_verify.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    library.crypto_sign_ed25519ph_init(state)
    library.crypto_sign_ed25519ph_update(state, data, len(data))
    return library.crypto_sign_ed25519ph_final_verify(state, signed, key) == 0


def descriptor(data, index):
    fields = struct.unpack_from("<HBBHHIIBBBBBbbB", data, HEADER_BYTES + index * DESCRIPTOR_BYTES)
    keys = ("slot", "orientation", "text_size", "width", "height", "offset", "bytes", "codec",
            "entries", "alpha", "flags", "anchor", "dx", "dy", "frames")
    asset = dict(zip(keys, fields))
    if asset["codec"] == art_codec.INDEXED:
        asset["entries"] += 1
    elif asset["codec"] != art_codec.LOSSLESS or asset["entries"]:
        raise ValueError("unknown encoding or a palette in a lossless asset")
    return asset


NOW_PLAYING_LOSSLESS = ("the now_playing slot refuses lossless images, which keep the processor too "
                        "busy behind Now Playing; use \"codec\": \"indexed\" for this image")
NOW_PLAYING_INDEXED = ("now_playing: an image behind Now Playing raises processor load and shortens "
                       "battery life while the screen is on")


def now_playing_check(slot, codec):
    if slot == SLOTS["now_playing"] and codec == art_codec.LOSSLESS:
        raise ValueError(NOW_PLAYING_LOSSLESS)


def descriptor_check(asset):
    dimensions(asset["slot"], asset["orientation"], asset["text_size"], asset["width"],
               asset["height"], asset["flags"])
    alpha_allowed = is_icon(asset["slot"])
    limit = asset["entries"] if asset["codec"] == art_codec.INDEXED else 1
    if asset["alpha"] > (limit if alpha_allowed else 0):
        raise ValueError("transparency in an opaque slot, or more alpha entries than colours")
    if asset["anchor"] or asset["dx"] or asset["dy"] or asset["frames"] != 1:
        raise ValueError("anchors, offsets and frame strips are not used by these slots")


def table_start(asset):
    palette = 2 * asset["entries"] + (asset["alpha"] + 1) // 2 if asset["entries"] else 0
    return 4 + palette


def payload_check(data, asset):
    payload = data[asset["offset"]:asset["offset"] + asset["bytes"]]
    expected = bytes((asset["codec"], max(asset["entries"] - 1, 0), asset["alpha"], 1))
    if payload[:4] != expected or len(payload) < table_start(asset) + 4 * (asset["height"] + 1):
        raise ValueError("payload header does not match its descriptor")
    info = art_codec.parse(payload, asset["width"], asset["height"])
    if info["codec"] == art_codec.INDEXED and asset["alpha"]:
        nibbles = payload[4 + 2 * asset["entries"]:table_start(asset)]
        if any(alpha == art_codec.OPAQUE for alpha in info["alphas"][:asset["alpha"]]) or (
                asset["alpha"] % 2 and nibbles[-1] >> 4):
            raise ValueError("invalid alpha entries")
    table = info["table"]
    if table[0] != table_start(asset) + 4 * (asset["height"] + 1) or table[-1] != asset["bytes"] or any(
            not 0 < b - a <= art_codec.ROW_MAX_BYTES for a, b in zip(table, table[1:])):
        raise ValueError("invalid row table")
    art_codec.decode(payload, asset["width"], asset["height"],
                     [[0] * asset["width"] for _ in range(asset["height"])])


def check(data, verify_key=None):
    if len(data) < HEADER_BYTES or len(data) > MAX_BYTES:
        raise ValueError("pack size is outside 512 bytes to 2 MiB")
    data = bytes(data)
    magic, version, header, total, expected = struct.unpack_from("<4sHHII", data)
    if magic == b"DSTP" and version == 1:
        raise ValueError("format version 1 packs are no longer read; rebuild from the source folder")
    base, flags, colour_count, count = data[16:20]
    sig_bytes, style_bytes = struct.unpack_from("<HH", data, 492)
    if (magic != b"DSTP" or version != VERSION or header != HEADER_BYTES or total != len(data)
            or base >= 5 or flags > 3 or colour_count != 20 or count > MAX_ASSETS
            or sig_bytes not in (0, SIGNATURE_BYTES) or style_bytes % 4
            or style_bytes > STYLE_MAX_BYTES or any(data[496:512])):
        raise ValueError("invalid header, version, flags, count or reserved bytes")
    identity = uuid.UUID(bytes=data[20:36])
    if not identity.int:
        raise ValueError("pack ID must be nonzero")
    info = {"id": str(identity), "name": text(data, 36, 32), "author": text(data, 68, 32),
            "version": text(data, 100, 32), "licence": text(data, 132, 64),
            "homepage": text(data, 196, 128, False, True),
            "gallery": text(data, 324, 128, False, True), "base": BASES[base], "assets": []}
    if not re.fullmatch(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})", info["version"]) or any(
            int(part) > 65535 for part in info["version"].split(".")):
        raise ValueError("version must be major.minor.patch, each 0 to 65535")
    end = total - sig_bytes
    style_at = HEADER_BYTES + count * DESCRIPTOR_BYTES
    next_offset = style_at + style_bytes
    if end < next_offset or zlib.crc32(data[:12] + data[16:end]) != expected:
        raise ValueError("CRC mismatch or truncated directory")
    style_entries(data[style_at:next_offset])
    seen = set()
    for index in range(count):
        asset = descriptor(data, index)
        descriptor_check(asset)
        now_playing_check(asset["slot"], asset["codec"])
        variant = (asset["slot"], asset["orientation"], asset["text_size"])
        if variant in seen or asset["offset"] != next_offset or asset["offset"] + asset["bytes"] > end:
            raise ValueError("duplicate, overlapping, missing or truncated asset")
        seen.add(variant)
        payload_check(data, asset)
        next_offset += asset["bytes"]
        info["assets"].append(asset)
    if next_offset != end:
        raise ValueError("unreferenced payload")
    info["signature"] = "unsigned"
    if sig_bytes:
        block = data[end:]
        if block[:8] != b"DSSG\x01\0\0\0":
            raise ValueError("invalid signature block")
        info["key_id"] = block[8:24].hex()
        info["signature"] = "unknown-key"
        if verify_key is not None:
            public = key_bytes(verify_key)
            if hashlib.sha256(public).digest()[:16] == block[8:24]:
                if not verify_signature(data[:end], block[24:], public):
                    raise ValueError("invalid Ed25519ph signature")
                info["signature"] = "verified"
    info["warnings"] = contrast_warnings(struct.unpack_from("<20H", data, 452))
    if any(asset["slot"] == SLOTS["now_playing"] for asset in info["assets"]):
        info["warnings"].append(NOW_PLAYING_INDEXED)
    return info


def psnr(reference, decoded):
    total = 0
    for first, second in zip(reference, decoded):
        for a, b in zip(first, second):
            for shift, mask in ((11, 31), (5, 63), (0, 31)):
                scale = 255 / mask
                total += (((a >> shift) & mask) * scale - ((b >> shift) & mask) * scale) ** 2
    count = 3 * len(reference) * len(reference[0])
    return 99.0 if total == 0 else round(10 * math.log10(255 ** 2 * count / total), 2)


def encode_asset(path, slot, settings):
    with Image.open(path) as image:
        rows = art_codec.image_rows(image)
        translucent = any(alpha < 255 for row in rows for *_, alpha in row)
        if translucent and not is_icon(slot):
            raise ValueError(f"{path.name}: only icons may be transparent; composite it first")
        codec = CODECS[settings["codec"]]
        pixels = art_codec.prepared(image, codec, settings["dither"])
        data = art_codec.payload(pixels, codec)
        reference = [[color for color, _ in row] for row in art_codec.reduced(rows)]
        decoded = art_codec.decode(data, image.width, image.height,
                                   [[0] * image.width for _ in range(image.height)])
        report = {"file": path.name, "codec": settings["codec"], "bytes": len(data),
                  "ratio": round(image.width * image.height * 2 / len(data), 2),
                  "psnr_db": psnr(reference, decoded)}
        return image.width, image.height, data, report


def asset_settings(asset, manifest):
    settings = {"codec": asset.get("codec", manifest.get("codec", "indexed")),
                "dither": asset.get("dither", manifest.get("dither", "none"))}
    if settings["codec"] not in CODECS or settings["dither"] not in ("none", "ordered", "fs"):
        raise ValueError("codec must be indexed or lossless; dither none, ordered or fs")
    return settings


def build(folder, sign_key=None, report=None):
    folder = Path(folder).resolve()
    manifest_path = folder / "theme.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else tomllib.loads(
        (folder / "theme.toml").read_text())
    allowed = {"id", "name", "author", "version", "licence", "homepage", "gallery", "base",
               "horizon", "header_rule", "palette", "assets", "dither", "codec", "style"}
    if set(manifest) - allowed:
        raise ValueError("unknown manifest fields")
    data = bytearray(HEADER_BYTES)
    data[:8] = struct.pack("<4sHH", b"DSTP", VERSION, HEADER_BYTES)
    data[16] = BASES.index(manifest["base"])
    data[17] = bool(manifest.get("horizon", False)) | (bool(manifest.get("header_rule", False)) << 1)
    data[18] = 20
    data[20:36] = uuid.UUID(manifest["id"]).bytes
    for field, start, size in (("name", 36, 32), ("author", 68, 32), ("version", 100, 32),
                               ("licence", 132, 64), ("homepage", 196, 128), ("gallery", 324, 128)):
        data[start:start + size] = padded(manifest.get(field, ""), size,
                                          field not in ("homepage", "gallery"),
                                          field in ("homepage", "gallery"))
    if set(manifest["palette"]) != set(ROLES):
        raise ValueError("palette must define every colour role exactly once")
    struct.pack_into("<20H", data, 452, *(rgb565(manifest["palette"][role]) for role in ROLES))
    assets = manifest.get("assets", [])
    if len(assets) > MAX_ASSETS:
        raise ValueError("at most 64 assets")
    style = style_table(manifest.get("style"))
    data[19] = len(assets)
    struct.pack_into("<H", data, 494, len(style))
    data += bytes(DESCRIPTOR_BYTES * len(assets)) + style
    for index, asset in enumerate(assets):
        if set(asset) - {"slot", "file", "orientation", "text_size", "codec", "dither"}:
            raise ValueError("unknown asset fields")
        path = (folder / asset["file"]).resolve()
        if not path.is_relative_to(folder) or path.suffix.lower() != ".png":
            raise ValueError("assets must be PNG files inside the source folder")
        slot = SLOTS[asset["slot"]]
        orientation = ORIENTATIONS[asset.get("orientation", "any")]
        size = TEXT_SIZES[asset.get("text_size", "any")]
        with Image.open(path) as image:
            width, height = image.size
        flags = TILE if slot == 3 and width <= 64 and height <= 64 else 0
        dimensions(slot, orientation, size, width, height, flags)
        settings = asset_settings(asset, manifest)
        if slot == SLOTS["now_playing"] and settings["codec"] == "lossless":
            raise ValueError(f"{asset['file']}: {NOW_PLAYING_LOSSLESS}")
        width, height, payload, stats = encode_asset(path, slot, settings)
        struct.pack_into("<HBBHHIIBBBBBbbB", data, HEADER_BYTES + index * DESCRIPTOR_BYTES, slot,
                         orientation, size, width, height, len(data), len(payload), payload[0],
                         payload[1], payload[2], flags, 0, 0, 0, 1)
        data += payload
        if report is not None:
            report.append(stats)
        if len(data) + (SIGNATURE_BYTES if sign_key else 0) > MAX_BYTES:
            raise ValueError("pack exceeds 2 MiB")
    struct.pack_into("<I", data, 8, len(data) + (SIGNATURE_BYTES if sign_key else 0))
    struct.pack_into("<H", data, 492, SIGNATURE_BYTES if sign_key else 0)
    struct.pack_into("<I", data, 12, zlib.crc32(data[:12] + data[16:]))
    if sign_key is not None:
        public, secret = key_pair(sign_key)
        signed = signature(bytes(data), secret)
        data += b"DSSG\x01\0\0\0" + hashlib.sha256(public).digest()[:16] + signed
    check(bytes(data))
    return bytes(data)


def preview(data, folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    written = []
    for index, asset in enumerate(check(data)["assets"]):
        payload = data[asset["offset"]:asset["offset"] + asset["bytes"]]
        width, height = asset["width"], asset["height"]
        under = [[0x7BEF if (x // 8 + y // 8) % 2 else 0xC618 for x in range(width)]
                 for y in range(height)]
        rows = art_codec.decode(payload, width, height, under)
        image = Image.new("RGB", (width, height))
        image.putdata([((value >> 11) << 3, ((value >> 5) & 63) << 2, (value & 31) << 3)
                       for row in rows for value in row])
        name = f"{index:02d}-slot{asset['slot']}-o{asset['orientation']}-t{asset['text_size']}.png"
        image.save(folder / name)
        written.append(name)
    return written


def main():
    parser = argparse.ArgumentParser(description="Build, check and preview data-only theme packs")
    parser.add_argument("folder", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path)
    parser.add_argument("--preview", type=Path, help="write each asset as the player draws it")
    parser.add_argument("--sign-key", type=Path, help="32-byte Ed25519 private seed; never included in the pack")
    parser.add_argument("--verify-key", type=Path, help="32-byte public key or 64 hex digits")
    parser.add_argument("--public-key-out", type=Path)
    args = parser.parse_args()
    try:
        report = []
        if args.check:
            data = args.check.read_bytes()
        else:
            if args.folder is None or args.output is None:
                parser.error("building requires a source folder and --output")
            data = build(args.folder, args.sign_key, report)
            destination = args.output
            if destination.is_dir():
                destination /= check(data)["id"] + ".gtheme"
            destination.write_bytes(data)
        info = check(data, args.verify_key)
        if report:
            info["encoding"] = report
            info["warnings"] += [f"{entry['file']}: {entry['psnr_db']} dB after quantisation"
                                 for entry in report if entry["psnr_db"] < 30]
        if args.preview:
            info["previews"] = preview(data, args.preview)
        if args.public_key_out:
            if args.sign_key is None:
                parser.error("--public-key-out requires --sign-key")
            args.public_key_out.write_bytes(public_key(args.sign_key))
        print(json.dumps(info, ensure_ascii=False, indent=2))
    except (ValueError, KeyError, OSError, UnicodeError, struct.error) as error:
        parser.exit(1, f"theme pack: {error}\n")


if __name__ == "__main__":
    main()
