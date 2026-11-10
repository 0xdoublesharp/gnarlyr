import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

MAGIC = b"DSLP"
VERSION = 1
CODE_BYTES = 8
NAME_BYTES = 28
HEADER = struct.Struct(f"<4sHHIII{CODE_BYTES}s{NAME_BYTES}s")
CHECKSUM_OFFSET = 16
CHECKSUM_END = CHECKSUM_OFFSET + 4
PACK_LIMIT = 0x2000
CODE_LIMIT = 5
ENGLISH = "en"
PSEUDO = "qps"
PSEUDO_NAME = "[Pseudo]"
NAME_ID = "STR_LANGUAGE_NAME"
HASH_START = 2166136261
HASH_PRIME = 16777619
WORD = 0xFFFFFFFF
LINE = re.compile(r"^(STR_[A-Z0-9_]+)\s*=\s*(.*)$")
CODE = re.compile(r"^[A-Za-z0-9-]+$")
PLACEHOLDER = re.compile(r"\{(\d)\}")
PIECE = re.compile(r"\{\d\}|[^{]+|\{")
ACCENTS = str.maketrans("aeiouyAEIOUY", "áéíóúýÁÉÍÓÚÝ")
ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = ROOT / "assets" / "lang"
FONTS = ROOT / "assets" / "fonts"
HEADER_OUTPUT = "include/core/strings.h"
ENGLISH_OUTPUT = "strings_english.h"


class PackError(Exception):
    pass


def fnv(data, value=HASH_START):
    for byte in data:
        value = ((value ^ byte) * HASH_PRIME) & WORD
    return value


def read_entries(path):
    """[(name, text)] of a language file, in file order."""
    entries = []
    seen = set()
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = LINE.match(line)
        if not match:
            raise PackError(f"{path.name}:{number}: expected STR_NAME = text")
        name, text = match.group(1), match.group(2).strip().replace("\\n", "\n")
        if name in seen:
            raise PackError(f"{path.name}:{number}: {name} appears twice")
        seen.add(name)
        entries.append((name, text))
    return entries


def read_catalogue(directory=LANGUAGES):
    catalogue = read_entries(Path(directory) / f"{ENGLISH}.txt")
    names = [name for name, _ in catalogue]
    if NAME_ID not in names:
        raise PackError(f"{ENGLISH}.txt has no {NAME_ID}")
    for name, text in catalogue:
        if not text:
            raise PackError(f"{ENGLISH}.txt: {name} is empty")
        wanted = sorted(set(PLACEHOLDER.findall(text)))
        if wanted != [str(index) for index in range(len(wanted))]:
            raise PackError(f"{ENGLISH}.txt: {name} must number its placeholders from 0")
    return catalogue


def catalogue_hash(catalogue):
    return fnv(b"".join(name.encode("ascii") + b"\0" for name, _ in catalogue))


def read_translations(path, catalogue):
    known = {name for name, _ in catalogue}
    translations = dict(read_entries(path))
    unknown = sorted(set(translations) - known)
    if unknown:
        raise PackError(f"{path.name}: unknown ids: " + ", ".join(unknown))
    return translations


def check_placeholders(code, catalogue, translations):
    for name, english in catalogue:
        text = translations.get(name, "")
        if text and set(PLACEHOLDER.findall(text)) != set(PLACEHOLDER.findall(english)):
            raise PackError(f"{code}: {name} must use the placeholders of its English text")


def range_characters(ranges):
    return {codepoint for first, last in ranges for codepoint in range(first, last + 1)}


def drawable_characters(fonts_directory=FONTS):
    """The characters every text size can draw."""
    fonts = json.loads((Path(fonts_directory) / "fonts.json").read_text(encoding="utf-8"))
    packs = json.loads((Path(fonts_directory) / "glyph_pack.json").read_text(encoding="utf-8"))
    faces = {face["name"]: range_characters(face["ranges"]) for face in packs["faces"]}
    shared = range_characters(fonts["character_ranges"])
    drawable = None
    for font in fonts["fonts"]:
        own = shared | range_characters(font.get("extra_ranges", []))
        own |= faces.get(font.get("pack_face"), set())
        drawable = own if drawable is None else drawable & own
    return drawable


def check_glyphs(code, texts, drawable):
    for name, text in texts:
        missing = sorted({character for character in text
                          if ord(character) not in drawable and character != "\n"})
        if missing:
            shown = " ".join(f"{character} U+{ord(character):04X}" for character in missing)
            raise PackError(f"{code}: {name} uses characters a text size cannot draw: {shown}")


def padded(text, size, what):
    data = text.encode("utf-8")
    if len(data) >= size:
        raise PackError(f"{what} {text!r} is longer than {size - 1} bytes")
    return data.ljust(size, b"\0")


def build_pack(code, catalogue, translations):
    if not CODE.match(code) or len(code) > CODE_LIMIT:
        raise PackError(f"language code {code!r} must be 1 to {CODE_LIMIT} letters, digits or '-'")
    texts = [(name, translations.get(name) or english) for name, english in catalogue]
    body = bytearray()
    offsets = []
    for _, text in texts:
        offsets.append(len(body))
        body += text.encode("utf-8") + b"\0"
    if len(body) > 0x10000:
        raise PackError("the strings are too long for 16-bit offsets")
    table = b"".join(struct.pack("<H", offset) for offset in offsets)
    size = HEADER.size + len(table) + len(body)
    if size > PACK_LIMIT:
        raise PackError(f"{code}: the pack is {size:,} bytes, over the {PACK_LIMIT:,} byte limit")
    name = dict(texts)[NAME_ID]
    header = HEADER.pack(MAGIC, VERSION, len(catalogue), size, catalogue_hash(catalogue), 0,
                         padded(code, CODE_BYTES, "language code"),
                         padded(name, NAME_BYTES, "language name"))
    pack = bytearray(header + table + bytes(body))
    checksum = fnv(pack[CHECKSUM_END:], fnv(pack[:CHECKSUM_OFFSET]))
    struct.pack_into("<I", pack, CHECKSUM_OFFSET, checksum)
    return bytes(pack)


def pack_code(pack):
    """The language code of a sound pack; PackError names what is wrong with any other."""
    if len(pack) < HEADER.size:
        raise PackError("shorter than a pack header")
    magic, version, count, size, _, checksum, code, _ = HEADER.unpack_from(pack)
    if magic != MAGIC or version != VERSION:
        raise PackError("not a version 1 language pack")
    if size != len(pack) or size > PACK_LIMIT or size <= HEADER.size + 2 * count:
        raise PackError(f"its size field is {size:,}, the file is {len(pack):,} bytes")
    if checksum != fnv(pack[CHECKSUM_END:], fnv(pack[:CHECKSUM_OFFSET])):
        raise PackError("its checksum is wrong")
    return code.rstrip(b"\0").decode("ascii")


def pseudo_text(text):
    """An accented, longer, bracketed stand-in that keeps the placeholders."""
    pieces = [piece if PLACEHOLDER.fullmatch(piece) else piece.translate(ACCENTS)
              for piece in PIECE.findall(text)]
    letters = sum(1 for character in PLACEHOLDER.sub("", text) if character.isalpha())
    return "[" + "".join(pieces) + "!" * ((letters + 2) // 3) + "]"


def pseudo_translations(catalogue):
    translations = {name: pseudo_text(text) for name, text in catalogue}
    translations[NAME_ID] = PSEUDO_NAME
    return translations


def language_pack(code, directory=LANGUAGES, fonts_directory=FONTS):
    catalogue = read_catalogue(directory)
    if code == PSEUDO:
        translations = pseudo_translations(catalogue)
    elif code == ENGLISH:
        translations = {}
    else:
        translations = read_translations(Path(directory) / f"{code}.txt", catalogue)
    check_placeholders(code, catalogue, translations)
    texts = [(name, translations.get(name) or english) for name, english in catalogue]
    check_glyphs(code, texts, drawable_characters(fonts_directory))
    return build_pack(code, catalogue, translations)


def c_literal(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"' + escaped + '"'


def header_source(catalogue):
    lines = ["#pragma once", "", "#include <stdint.h>", "", "typedef enum StringId {"]
    lines += [f"    {name}," for name, _ in catalogue]
    lines += ["    STRING_COUNT", "} StringId;", "",
              f"#define STRINGS_HASH UINT32_C(0x{catalogue_hash(catalogue):08X})", ""]
    return "\n".join(lines)


def english_source(catalogue):
    lines = ["#pragma once", "", '#include "core/strings.h"', "",
             "static const char *const STRINGS_ENGLISH[STRING_COUNT] = {"]
    lines += [f"    [{name}] = {c_literal(text)}," for name, text in catalogue]
    lines += ["};", ""]
    return "\n".join(lines)


def formatted(source, style_file):
    formatter = shutil.which("clang-format")
    if not formatter or style_file is None:
        return source
    result = subprocess.run([formatter, f"--style=file:{style_file}", "--assume-filename=strings.h"],
                            input=source, check=True, capture_output=True, text=True)
    return result.stdout


def generated_sources(catalogue, style_file):
    return {HEADER_OUTPUT: formatted(header_source(catalogue), style_file),
            ENGLISH_OUTPUT: formatted(english_source(catalogue), style_file)}


def write_sources(arguments):
    outputs = generated_sources(read_catalogue(arguments.languages), arguments.style)
    stale = []
    for name, content in outputs.items():
        path = arguments.sources / name
        if arguments.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                stale.append(str(path))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
    for path in stale:
        print(f"out of date: {path}", file=sys.stderr)
    return 1 if stale else 0


def write_pack(arguments):
    pack = language_pack(arguments.pack, arguments.languages)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(pack)
    print(f"{arguments.pack}: {len(pack):,} of {PACK_LIMIT:,} bytes")
    return 0


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Build language packs and the string catalogue")
    parser.add_argument("--languages", type=Path, default=LANGUAGES,
                        help="folder with en.txt and the translations")
    parser.add_argument("--sources", type=Path, help="src/core: write the generated C headers there")
    parser.add_argument("--style", type=Path, help=".clang-format for the generated headers")
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed headers differ from a fresh build")
    parser.add_argument("--pack", help=f"language code to pack; {PSEUDO} is the test language")
    parser.add_argument("--output", type=Path, help="pack file to write")
    arguments = parser.parse_args(argv)
    if bool(arguments.pack) != bool(arguments.output):
        parser.error("--pack and --output go together")
    if not arguments.pack and not arguments.sources:
        parser.error("nothing to do: give --sources or --pack")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        status = write_sources(arguments) if arguments.sources else 0
        return status or (write_pack(arguments) if arguments.pack else 0)
    except PackError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
