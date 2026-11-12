import argparse
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import tomllib
import uuid
import zlib

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HEADER_BYTES = 512
MAX_BYTES = 4194304
MAX_ASSETS = 32
SIGNATURE_BYTES = 88
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


def dimensions(slot, orientation, size, width, height):
    if orientation not in (0, 1, 2) or size not in (0, 1, 2, 255) or width < 1 or height < 1:
        raise ValueError("invalid asset variant or empty dimensions")
    if 257 <= slot < 256 + ICON_NAMES.index("COUNT"):
        name = ICON_NAMES[slot - 256]
        narrow = name in ("BOLT", "BLUETOOTH_SMALL", "LOCK", "SONG_SMALL")
        small = slot >= SLOTS["icon_play"]
        expected = (9 if narrow else 14, 14) if small else (20, 20)
        if (width, height) == expected:
            return
    elif slot == 5 and size == 255 and (width, height) == (160, 48):
        return
    elif slot in (1, 2, 3, 4) and size == 255:
        if slot == 3 and width <= 64 and height <= 64:
            return
        if orientation < 2 and (width, height) == ((480, 222) if orientation == 0 else (222, 480)):
            return
    raise ValueError(f"wrong dimensions for slot {slot}: {width}x{height}")


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


def check(data, verify_key=None):
    if len(data) < HEADER_BYTES or len(data) > MAX_BYTES:
        raise ValueError("pack size is outside 512 bytes to 4 MiB")
    data = bytes(data)
    magic, version, header, total, expected = struct.unpack_from("<4sHHII", data)
    base, flags, colour_count, count = data[16:20]
    sig_bytes, = struct.unpack_from("<H", data, 492)
    if (magic != b"DSTP" or version != 1 or header != HEADER_BYTES or total != len(data)
            or base >= 5 or flags > 3 or colour_count != 20 or count > MAX_ASSETS
            or sig_bytes not in (0, SIGNATURE_BYTES) or any(data[494:512])):
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
    if end < HEADER_BYTES or zlib.crc32(data[:12] + data[16:end]) != expected:
        raise ValueError("CRC mismatch")
    next_offset = HEADER_BYTES + count * 16
    seen = set()
    for index in range(count):
        if HEADER_BYTES + (index + 1) * 16 > end:
            raise ValueError("truncated directory")
        slot, orientation, size, width, height, offset, length = struct.unpack_from(
            "<HBBHHII", data, HEADER_BYTES + index * 16)
        dimensions(slot, orientation, size, width, height)
        variant = (slot, orientation, size)
        if (variant in seen or offset != next_offset or length != width * height * 2
                or offset + length > end):
            raise ValueError("duplicate, overlapping, missing or truncated asset")
        seen.add(variant)
        next_offset += length
        info["assets"].append({"slot": slot, "orientation": orientation, "text_size": size,
                                "width": width, "height": height, "offset": offset, "bytes": length})
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
    return info


def pixels(image, dither):
    values = bytearray()
    matrix = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
    for y in range(image.height):
        for x in range(image.width):
            red, green, blue = image.getpixel((x, y))
            adjustment = matrix[y % 4][x % 4] - 8 if dither else 0
            red = max(0, min(255, red + adjustment // 2))
            green = max(0, min(255, green + adjustment // 4))
            blue = max(0, min(255, blue + adjustment // 2))
            values += struct.pack("<H", ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3))
    return bytes(values)


def build(folder, sign_key=None):
    folder = Path(folder).resolve()
    manifest_path = folder / "theme.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else tomllib.loads(
        (folder / "theme.toml").read_text())
    allowed = {"id", "name", "author", "version", "licence", "homepage", "gallery", "base",
               "horizon", "header_rule", "palette", "assets", "dither"}
    if set(manifest) - allowed:
        raise ValueError("unknown manifest fields")
    data = bytearray(HEADER_BYTES)
    data[:8] = struct.pack("<4sHH", b"DSTP", 1, HEADER_BYTES)
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
        raise ValueError("at most 32 assets")
    data[19] = len(assets)
    data += bytes(16 * len(assets))
    for index, asset in enumerate(assets):
        if set(asset) - {"slot", "file", "orientation", "text_size"}:
            raise ValueError("unknown asset fields")
        path = (folder / asset["file"]).resolve()
        if not path.is_relative_to(folder) or path.suffix.lower() != ".png":
            raise ValueError("assets must be PNG files inside the source folder")
        slot = SLOTS[asset["slot"]]
        orientation = ORIENTATIONS[asset.get("orientation", "any")]
        size = TEXT_SIZES[asset.get("text_size", "any")]
        with Image.open(path) as image:
            dimensions(slot, orientation, size, image.width, image.height)
            if "A" in image.mode or "transparency" in image.info:
                raise ValueError("RGB565 assets must be opaque; composite transparency first")
            encoded = pixels(image.convert("RGB"), manifest.get("dither", False))
            struct.pack_into("<HBBHHII", data, HEADER_BYTES + index * 16, slot, orientation, size,
                             image.width, image.height, len(data), len(encoded))
            data += encoded
        if len(data) + (SIGNATURE_BYTES if sign_key else 0) > MAX_BYTES:
            raise ValueError("pack exceeds 4 MiB")
    struct.pack_into("<I", data, 8, len(data) + (SIGNATURE_BYTES if sign_key else 0))
    struct.pack_into("<H", data, 492, SIGNATURE_BYTES if sign_key else 0)
    struct.pack_into("<I", data, 12, zlib.crc32(data[:12] + data[16:]))
    if sign_key is not None:
        public, secret = key_pair(sign_key)
        signed = signature(bytes(data), secret)
        data += b"DSSG\x01\0\0\0" + hashlib.sha256(public).digest()[:16] + signed
    check(bytes(data))
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description="Build and validate data-only RGB565 theme packs")
    parser.add_argument("folder", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path)
    parser.add_argument("--sign-key", type=Path, help="32-byte Ed25519 private seed; never included in the pack")
    parser.add_argument("--verify-key", type=Path, help="32-byte public key or 64 hex digits")
    parser.add_argument("--public-key-out", type=Path)
    args = parser.parse_args()
    try:
        if args.check:
            data = args.check.read_bytes()
        else:
            if args.folder is None or args.output is None:
                parser.error("building requires a source folder and --output")
            data = build(args.folder, args.sign_key)
            destination = args.output
            if destination.is_dir():
                destination /= check(data)["id"] + ".gtheme"
            destination.write_bytes(data)
        info = check(data, args.verify_key)
        if args.public_key_out:
            if args.sign_key is None:
                parser.error("--public-key-out requires --sign-key")
            args.public_key_out.write_bytes(public_key(args.sign_key))
        print(json.dumps(info, ensure_ascii=False, indent=2))
    except (ValueError, KeyError, OSError, UnicodeError, struct.error) as error:
        parser.exit(1, f"theme pack: {error}\n")


if __name__ == "__main__":
    main()
