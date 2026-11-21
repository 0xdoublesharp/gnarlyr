"""Renders glyph faces the player reads on demand (src/gfx/font_pack.c), and the extra.font
container that carries seven more faces on the player's internal storage.

A face is one blob, little-endian, in 512-byte sectors:
  sector 0   header: "EGLF", version 1, entry size (12), entries per index sector (42),
             glyph count, blob size, first and last codepoint, directory sector count,
             largest bitmap, the first codepoint of every directory sector (8 slots);
             at 40 the FNV-1a of bytes 0-39, at 44 the image CRC of bytes 48 to the end
  then       directory sectors: the first codepoint (u16) of up to 254 index sectors each
  then       index sectors: 42 entries of 12 bytes sorted by codepoint:
             codepoint (u16), width, height, left (s8), top (s8), advance, 0, bitmap offset (u32)
  then       bitmaps: 4-bit coverage, row by row, high nibble first, as in src/fonts
Every directory and index sector ends in the FNV-1a of its first 508 bytes, seeded with the
header's FNV-1a, so the player checks each sector when it reads it.

extra.font: sector 0 holds "EGXF", version 1, face count (7), language count, file size,
(offset, size) per face, 32 language codes of 8 bytes, and at 508 the FNV-1a of bytes 0-507;
the faces follow, each starting on a sector.
"""
import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from fontTools.ttLib import TTFont

import font_build
import make_image
from lang_pack import fnv

MAGIC = b"EGLF"
VERSION = 1
SECTOR_BYTES = 512
ENTRY = struct.Struct("<HBBbbBBI")
ENTRIES_PER_SECTOR = 42
CHECK_OFFSET = SECTOR_BYTES - 4
DIRECTORY_ENTRIES = CHECK_OFFSET // 2
MAX_ROOT = 8
HEADER = struct.Struct("<4sBBHIIHHHH16s")
HEADER_CHECK = HEADER.size
ROOT_OFFSET = HEADER_CHECK - 2 * MAX_ROOT
HEADER_BYTES = HEADER_CHECK + 8
MAX_BITMAP_BYTES = 96
MAX_CODEPOINT = 0xFFFF
MAX_GLYPHS = MAX_CODEPOINT + 1

EXTRA_MAGIC = b"EGXF"
EXTRA_FACES = 7
EXTRA_LANGUAGES = 32
EXTRA_CODE_BYTES = 8
EXTRA_HEADER = struct.Struct("<4sBBBxI")
EXTRA_ROW = struct.Struct("<II")
EXTRA_LANGUAGES_OFFSET = EXTRA_HEADER.size + EXTRA_FACES * EXTRA_ROW.size
LANGUAGE_CODE = re.compile(r"[A-Za-z0-9-]{1,5}")


class PackError(Exception):
    pass


@dataclass
class Face:
    name: str
    blob: bytes
    glyph_count: int
    dropped: list


def face_characters(face):
    return {codepoint for first, last in face["ranges"] for codepoint in range(first, last + 1)}


def face_numbers(config):
    """Face names to the 1-based numbers fonts use in pack_face (0 means none)."""
    return {face["name"]: number for number, face in enumerate(config["faces"], start=1)}


def covered(path, codepoints):
    cmap = TTFont(str(path), fontNumber=0)["cmap"].getBestCmap()
    return sorted(codepoint for codepoint in codepoints
                  if codepoint in cmap and codepoint <= MAX_CODEPOINT)


def padded(data):
    return data + bytes(-len(data) % SECTOR_BYTES)


def checked_sector(body, seed):
    sector = body + bytes(CHECK_OFFSET - len(body))
    return sector + struct.pack("<I", fnv(sector, seed))


def bitmap_bytes(glyph):
    return (glyph.width * glyph.height + 1) // 2


def index_sectors(glyphs, data_offset, seed):
    sectors = []
    offset = data_offset
    for start in range(0, len(glyphs), ENTRIES_PER_SECTOR):
        sector = bytearray()
        for glyph in glyphs[start:start + ENTRIES_PER_SECTOR]:
            sector += ENTRY.pack(glyph.codepoint, glyph.width, glyph.height, glyph.left,
                                 glyph.top, glyph.advance, 0, offset)
            offset += bitmap_bytes(glyph)
        sectors.append(checked_sector(bytes(sector), seed))
    return sectors


def directory_sectors(firsts, seed):
    return [checked_sector(b"".join(struct.pack("<H", first)
                                    for first in firsts[start:start + DIRECTORY_ENTRIES]), seed)
            for start in range(0, len(firsts), DIRECTORY_ENTRIES)]


def check_glyph(glyph):
    for value, low, high, name in ((glyph.codepoint, 0, MAX_CODEPOINT, "codepoint"),
                                   (glyph.width, 0, 255, "width"), (glyph.height, 0, 255, "height"),
                                   (glyph.left, -128, 127, "left"), (glyph.top, -128, 127, "top"),
                                   (glyph.advance, 0, 255, "advance")):
        if not low <= value <= high:
            raise PackError(f"U+{glyph.codepoint:04X}: {name} {value} is out of range")


def check_glyphs(glyphs):
    if not glyphs:
        raise PackError("a face needs at least one glyph")
    for glyph in glyphs:
        check_glyph(glyph)
    codepoints = [glyph.codepoint for glyph in glyphs]
    if any(later <= earlier for earlier, later in zip(codepoints, codepoints[1:])):
        raise PackError("glyphs must be sorted by codepoint, each once")


def face_layout(count):
    sectors = (count + ENTRIES_PER_SECTOR - 1) // ENTRIES_PER_SECTOR
    directories = (sectors + DIRECTORY_ENTRIES - 1) // DIRECTORY_ENTRIES
    return sectors, directories


def face_header(glyphs, size, roots):
    largest = max(bitmap_bytes(glyph) for glyph in glyphs)
    root = b"".join(struct.pack("<H", first) for first in roots).ljust(2 * MAX_ROOT, b"\0")
    fields = HEADER.pack(MAGIC, VERSION, ENTRY.size, ENTRIES_PER_SECTOR, len(glyphs), size,
                         glyphs[0].codepoint, glyphs[-1].codepoint, len(roots), largest, root)
    return fields + struct.pack("<I", fnv(fields))


def pack_glyphs(glyphs):
    """The face blob for glyphs, which must be sorted by codepoint and fit the limits."""
    check_glyphs(glyphs)
    sectors, directories = face_layout(len(glyphs))
    data_offset = (1 + directories + sectors) * SECTOR_BYTES
    data = b"".join(font_build.pack_nibbles(glyph.levels) for glyph in glyphs)
    firsts = [glyph.codepoint for glyph in glyphs[::ENTRIES_PER_SECTOR]]
    header = face_header(glyphs, data_offset + len(data), firsts[::DIRECTORY_ENTRIES])
    seed = fnv(header[:HEADER_CHECK])
    body = b"".join(directory_sectors(firsts, seed) + index_sectors(glyphs, data_offset, seed))
    tail = bytes(SECTOR_BYTES - HEADER_BYTES) + body + data
    return header + struct.pack("<I", make_image.crc32(tail)) + tail


def build_face(spec, codepoints, font_directory):
    """Renders a face."""
    path = font_directory / spec["file"]
    font = font_build.load_font(path, spec["size"])
    glyphs, dropped = [], []
    for codepoint in covered(path, codepoints):
        glyph = font_build.render_glyph(font, codepoint)
        if (glyph.width * glyph.height + 1) // 2 > spec.get("max_bitmap", MAX_BITMAP_BYTES):
            dropped.append(codepoint)
        else:
            glyphs.append(glyph)
    return Face(spec["name"], pack_glyphs(glyphs), len(glyphs), dropped)


def sector_at(blob, number, seed):
    sector = blob[number * SECTOR_BYTES:(number + 1) * SECTOR_BYTES]
    if len(sector) != SECTOR_BYTES or fnv(sector[:CHECK_OFFSET], seed) != \
            struct.unpack_from("<I", sector, CHECK_OFFSET)[0]:
        raise PackError(f"sector {number} is damaged")
    return sector


def face_fields(blob):
    if len(blob) < SECTOR_BYTES:
        raise PackError("the face is shorter than its header")
    fields = HEADER.unpack_from(blob)
    magic, version, entry_size, per_sector, count, size, first, last, roots, _, _ = fields
    seed, crc = struct.unpack_from("<II", blob, HEADER_CHECK)
    if magic != MAGIC or version != VERSION or entry_size != ENTRY.size or \
            per_sector != ENTRIES_PER_SECTOR:
        raise PackError("not a version 1 glyph pack face")
    if fnv(blob[:HEADER_CHECK]) != seed:
        raise PackError("the face header is damaged")
    if size != len(blob) or make_image.crc32(blob[HEADER_BYTES:]) != crc:
        raise PackError("the face size or CRC doesn't match")
    if not 0 < count <= MAX_GLYPHS or face_layout(count)[1] != roots or roots > MAX_ROOT or \
            first > last:
        raise PackError("the face header doesn't match its glyph count")
    return fields, seed


def face_entries(blob, count, roots, seed):
    sectors = face_layout(count)[0]
    root = struct.unpack_from(f"<{roots}H", blob, ROOT_OFFSET)
    directories = [sector_at(blob, 1 + number, seed) for number in range(roots)]
    for number in range(sectors):
        directory, within = divmod(number, DIRECTORY_ENTRIES)
        index = sector_at(blob, 1 + roots + number, seed)
        first = ENTRY.unpack_from(index)[0]
        if struct.unpack_from("<H", directories[directory], 2 * within)[0] != first or \
                (within == 0 and root[directory] != first):
            raise PackError(f"the directory doesn't match index sector {number}")
        for position in range(min(ENTRIES_PER_SECTOR, count - number * ENTRIES_PER_SECTOR)):
            yield ENTRY.unpack_from(index, position * ENTRY.size)


def read_face(blob):
    """Parses a face blob back into {codepoint: (width, height, left, top, advance, bitmap)}."""
    fields, seed = face_fields(blob)
    count, size, first, last, roots = fields[4], fields[5], fields[6], fields[7], fields[8]
    data = (1 + roots + face_layout(count)[0]) * SECTOR_BYTES
    glyphs = {}
    previous = first - 1
    for codepoint, width, height, left, top, advance, _, bitmap in \
            face_entries(blob, count, roots, seed):
        length = (width * height + 1) // 2
        if not data <= bitmap <= size - length or not previous < codepoint <= last:
            raise PackError(f"U+{codepoint:04X} is out of place")
        previous = codepoint
        glyphs[codepoint] = (width, height, left, top, advance, blob[bitmap:bitmap + length])
    return glyphs


def check_language(code):
    if not isinstance(code, str) or not LANGUAGE_CODE.fullmatch(code):
        raise PackError(f"{code!r} is not a language code of 1 to 5 letters, digits or dashes")
    return code


def pack_extra_font(faces, languages):
    """The extra.font container for 7 face blobs (None for an empty face) built to cover the
    given language codes."""
    if len(faces) != EXTRA_FACES:
        raise PackError(f"extra.font holds {EXTRA_FACES} faces, not {len(faces)}")
    codes = sorted({check_language(code) for code in languages})
    if len(codes) > EXTRA_LANGUAGES:
        raise PackError(f"extra.font lists at most {EXTRA_LANGUAGES} languages")
    rows, body = [], b""
    for face in faces:
        if face:
            read_face(face)
            rows.append((SECTOR_BYTES + len(body), len(face)))
            body += padded(face)
        else:
            rows.append((0, 0))
    header = EXTRA_HEADER.pack(EXTRA_MAGIC, VERSION, EXTRA_FACES, len(codes),
                               SECTOR_BYTES + len(body))
    header += b"".join(EXTRA_ROW.pack(*row) for row in rows)
    header += b"".join(code.encode("ascii").ljust(EXTRA_CODE_BYTES, b"\0") for code in codes)
    header = header.ljust(CHECK_OFFSET, b"\0")
    return header + struct.pack("<I", fnv(header)) + body


def extra_rows(blob):
    if len(blob) < SECTOR_BYTES:
        raise PackError("extra.font is shorter than its header")
    magic, version, count, languages, size = EXTRA_HEADER.unpack_from(blob)
    if magic != EXTRA_MAGIC or version != VERSION or count != EXTRA_FACES or \
            languages > EXTRA_LANGUAGES:
        raise PackError("not a version 1 extra.font")
    if fnv(blob[:CHECK_OFFSET]) != struct.unpack_from("<I", blob, CHECK_OFFSET)[0] or \
            size != len(blob):
        raise PackError("the extra.font header or size doesn't match")
    rows = [EXTRA_ROW.unpack_from(blob, EXTRA_HEADER.size + face * EXTRA_ROW.size)
            for face in range(EXTRA_FACES)]
    return rows, languages


def read_extra_font(blob):
    """Parses extra.font back into ([read_face output or None] * 7, [language codes])."""
    rows, count = extra_rows(blob)
    faces = []
    for offset, size in rows:
        if size and (offset % SECTOR_BYTES or offset < SECTOR_BYTES or offset + size > len(blob)):
            raise PackError("an extra.font face lies outside the file")
        faces.append(read_face(blob[offset:offset + size]) if size else None)
    codes = [blob[EXTRA_LANGUAGES_OFFSET + EXTRA_CODE_BYTES * index:][:EXTRA_CODE_BYTES]
             for index in range(count)]
    return faces, [check_language(code.rstrip(b"\0").decode("ascii", "replace")) for code in codes]


def parse_arguments():
    parser = argparse.ArgumentParser(description="Render glyph packs for the player")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--fonts", type=Path,
                        help="folder holding the font files the config names (default: its folder)")
    parser.add_argument("--output", type=Path, required=True, help="folder for <face>.bin")
    parser.add_argument("--check", action="store_true",
                        help="fail if a committed face differs from a fresh build")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    config = json.loads(arguments.config.read_text(encoding="utf-8-sig"))
    font_directory = arguments.fonts or arguments.config.parent
    stale = []
    for spec in config["faces"]:
        face = build_face(spec, face_characters(spec), font_directory)
        path = arguments.output / f"{spec['name']}.bin"
        if arguments.check:
            if not path.exists() or path.read_bytes() != face.blob:
                print(f"out of date: {path}")
                stale.append(path)
        else:
            arguments.output.mkdir(parents=True, exist_ok=True)
            path.write_bytes(face.blob)
            dropped = ", ".join(f"U+{codepoint:04X}" for codepoint in face.dropped) or "none"
            print(f"{face.name}: {face.glyph_count} glyphs, {len(face.blob):,} bytes; "
                  f"too big: {dropped}")
    return 1 if stale else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PackError as error:
        print(f"glyph_pack: {error}", file=sys.stderr)
        sys.exit(1)
