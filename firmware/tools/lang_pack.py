import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

MAGIC = b"DSLP"
VERSION = 2
LEGACY_VERSION = 1
CODE_BYTES = 8
NAME_BYTES = 28
HEADER = struct.Struct(f"<4sHHIII{CODE_BYTES}s{NAME_BYTES}s")
SEGMENT_INDEX = struct.Struct("<HH")
SEGMENT = struct.Struct("<HHIII")
CHECKSUM_OFFSET = 16
CHECKSUM_END = CHECKSUM_OFFSET + 4
PACK_LIMIT = 0x4800
FILE_LIMIT = 0xFFFF
MEMORY_ALIGN = 4
RESIDENT = 0
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
LAYOUT_HEADER = ROOT / "src" / "core" / HEADER_OUTPUT
MODULE_LISTS = Path("cmake") / "screen_modules.cmake"
MODULE_NUMBERS = Path("src") / "app" / "screen_modules.h"
SCANNED = ("src", "platform")
GENERATED = {"strings.h", "strings_english.h"}
RETAINING_CALLS = ("app_show_notice",)
NAMED = re.compile(r"\bSTR_[A-Z0-9_]+\b")
RUN_BASE = re.compile(r"\b(STR_[A-Z0-9_]+)\s*\+")
CMAKE_LIST = re.compile(r"\b(?:set|list)\(\s*(?:APPEND\s+)?(ECHO_SCREEN_MODULE\w*)\s+([^)]*)\)")
CMAKE_VARIABLE = re.compile(r"\$\{(\w+)\}")
MODULE_ENUM = re.compile(r"\bSCREEN_MODULE_([A-Z]+)\s*(?:=\s*(\d+))?\s*,")
LIST_PREFIX = "ECHO_SCREEN_MODULE_SOURCES_"
SHARED_LISTS = ("ECHO_SCREEN_MODULE_UI_SOURCES", "ECHO_SCREEN_MODULE_SHARED_SOURCES")


class PackError(Exception):
    pass


def fnv(data, value=HASH_START):
    for byte in data:
        value = ((value ^ byte) * HASH_PRIME) & WORD
    return value


def entries_from_text(text, source):
    entries = []
    seen = set()
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = LINE.match(line)
        if not match:
            raise PackError(f"{source}:{number}: expected STR_NAME = text")
        name, text = match.group(1), match.group(2).strip().replace("\\n", "\n")
        if name in seen:
            raise PackError(f"{source}:{number}: {name} appears twice")
        seen.add(name)
        entries.append((name, text))
    return entries


def read_entries(path):
    return entries_from_text(path.read_text(encoding="utf-8"), path.name)


def read_catalogue_text(text, source="en.txt"):
    catalogue = entries_from_text(text, source)
    names = [name for name, _ in catalogue]
    if NAME_ID not in names:
        raise PackError(f"{source} has no {NAME_ID}")
    for name, text in catalogue:
        if not text:
            raise PackError(f"{source}: {name} is empty")
        wanted = sorted(set(PLACEHOLDER.findall(text)))
        if wanted != [str(index) for index in range(len(wanted))]:
            raise PackError(f"{source}: {name} must number its placeholders from 0")
    return catalogue


def read_catalogue(directory=LANGUAGES):
    path = Path(directory) / f"{ENGLISH}.txt"
    return read_catalogue_text(path.read_text(encoding="utf-8"), path.name)


def catalogue_hash(catalogue):
    return fnv(b"".join(name.encode("ascii") + b"\0" for name, _ in catalogue))


def resident_segments(catalogue):
    return [(RESIDENT, [name for name, _ in catalogue])]


def layout_hash(segments):
    return fnv(b"".join(name.encode("ascii") + b"\0" + bytes([module])
                        for module, names in segments for name in names))


def ordered_names(segments):
    return [name for _, names in segments for name in names]


def module_lists(text):
    lists = {}
    for match in CMAKE_LIST.finditer(text):
        name = match.group(1)
        values = []
        for word in match.group(2).split():
            reference = CMAKE_VARIABLE.fullmatch(word)
            values += lists.get(reference.group(1), []) if reference else [word]
        appending = "APPEND" in match.group(0).split(name)[0]
        lists[name] = lists.get(name, []) + values if appending else values
    return lists


def module_numbers(text):
    numbers = {}
    value = 0
    for name, explicit in MODULE_ENUM.findall(text):
        value = int(explicit) if explicit else value
        numbers[name.lower()] = value
        value += 1
    numbers.pop("none", None)
    numbers.pop("count", None)
    return numbers


def source_owners(lists, numbers):
    """Each module source's UI module number, or RESIDENT for code beside every UI module."""
    shared = {source for name in SHARED_LISTS for source in lists.get(name, [])}
    holders = {}
    for name, sources in lists.items():
        if name.startswith(LIST_PREFIX):
            for source in sources:
                holders.setdefault(source, set()).add(name[len(LIST_PREFIX):])
    owners = {}
    for source, modules in holders.items():
        only = next(iter(modules)) if len(modules) == 1 else None
        owners[source] = (numbers[only] if only in numbers and source not in shared and
                          source.endswith(".c") else RESIDENT)
    return owners


def call_arguments(text, call):
    for match in re.finditer(r"\b" + call + r"\s*\(", text):
        depth, end = 1, match.end()
        while end < len(text) and depth:
            depth += {"(": 1, ")": -1}.get(text[end], 0)
            end += 1
        yield text[match.end():end]


def source_names(root):
    named, retained, bases = {}, set(), set()
    for top in SCANNED:
        for path in sorted((Path(root) / top).rglob("*")):
            if path.suffix not in (".c", ".h") or path.name in GENERATED:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            relative = path.relative_to(root).as_posix()
            for name in set(NAMED.findall(text)):
                named.setdefault(name, set()).add(relative)
            for call in RETAINING_CALLS:
                for arguments in call_arguments(text, call):
                    retained |= set(NAMED.findall(arguments))
            bases |= set(RUN_BASE.findall(text))
    return named, retained, bases


def string_segments_from(catalogue, named, retained, bases, owners):
    """Resident first, then each UI module's own strings, all in catalogue order.

    A string goes into module M only when every source naming it belongs to M alone and no
    retaining call takes it. A computed run (STR_BASE + n) follows its base string.
    """
    segment = {}
    names = [name for name, _ in catalogue]
    for name in names:
        modules = {owners.get(source, RESIDENT) for source in named.get(name, ())}
        segment[name] = (modules.pop() if len(modules) == 1 and name not in retained
                         else RESIDENT)
    for index, base in enumerate(names):
        if base not in bases:
            continue
        for member in names[index + 1:]:
            if not named.get(member, set()) <= named.get(base, set()):
                break
            segment[member] = segment[base]
    modules = sorted(set(segment.values()) - {RESIDENT})
    return [(module, [name for name in names if segment[name] == module])
            for module in [RESIDENT] + modules]


def string_segments(catalogue, root=ROOT):
    root = Path(root)
    lists = module_lists((root / MODULE_LISTS).read_text(encoding="utf-8"))
    numbers = module_numbers((root / MODULE_NUMBERS).read_text(encoding="utf-8"))
    named, retained, bases = source_names(root)
    return string_segments_from(catalogue, named, retained, bases, source_owners(lists, numbers))


def segments_from_header(text, catalogue):
    """The string order and segments a generated strings.h declares."""
    body = re.search(r"typedef enum StringId \{(.*?)\bSTRING_COUNT\b", text, re.DOTALL)
    modules = re.search(r"#define STRING_SEGMENT_MODULES \{([^}]*)\}", text)
    firsts = re.search(r"#define STRING_SEGMENT_FIRSTS \{([^}]*)\}", text)
    if not body or not modules or not firsts:
        raise PackError("the strings header declares no string segments")
    order = re.findall(r"\b(STR_[A-Z0-9_]+)\b", body.group(1))
    numbers = [int(value) for value in modules.group(1).split(",")]
    bounds = [int(value) for value in firsts.group(1).split(",")]
    if (sorted(order) != sorted(name for name, _ in catalogue) or len(bounds) != len(numbers) + 1
            or bounds[0] != 0 or bounds[-1] != len(order) or bounds != sorted(bounds)):
        raise PackError("the strings header does not match the catalogue")
    return [(module, order[bounds[index]:bounds[index + 1]])
            for index, module in enumerate(numbers)]


def read_segments(catalogue, header=LAYOUT_HEADER):
    return segments_from_header(Path(header).read_text(encoding="utf-8"), catalogue)


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
    return drawable_from_config(fonts, packs)


def drawable_from_config(fonts, packs):
    faces = {face["name"]: range_characters(face["ranges"]) for face in packs["faces"]}
    shared = range_characters(fonts["character_ranges"])
    drawable = None
    for font in fonts["fonts"]:
        own = shared | range_characters(font.get("extra_ranges", []))
        own |= faces.get(font.get("pack_face"), set())
        for fallback in font.get("fallback_faces", []):
            own |= faces.get(fallback, set())
        drawable = own if drawable is None else drawable & own
    return drawable if drawable is not None else set()


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


def check_native_name(name):
    if not name or not name.isprintable():
        raise PackError("language name must be a nonempty, single printable line")


def checked_code(code):
    if not CODE.fullmatch(code) or len(code) > CODE_LIMIT:
        raise PackError(f"language code {code!r} must be 1 to {CODE_LIMIT} letters, digits or '-'")


def pack_texts(code, catalogue, translations):
    checked_code(code)
    texts = {name: translations.get(name) or english for name, english in catalogue}
    check_native_name(texts[NAME_ID])
    for identifier, text in texts.items():
        if "\0" in text:
            raise PackError(f"{code}: {identifier} contains an embedded NUL")
    return texts


def text_block(texts):
    """A 16-bit offset per string, then the zero-terminated strings."""
    body = bytearray()
    offsets = []
    for text in texts:
        offsets.append(len(body))
        body += text.encode("utf-8") + b"\0"
    if len(body) > 0x10000:
        raise PackError("the strings are too long for 16-bit offsets")
    return b"".join(struct.pack("<H", offset) for offset in offsets) + bytes(body)


def sealed(pack, end):
    checksum = fnv(pack[CHECKSUM_END:end], fnv(pack[:CHECKSUM_OFFSET]))
    struct.pack_into("<I", pack, CHECKSUM_OFFSET, checksum)
    return bytes(pack)


def header_bytes(version, code, name, fields):
    count, size, catalogue = fields
    return HEADER.pack(MAGIC, version, count, size, catalogue, 0,
                       padded(code, CODE_BYTES, "language code"),
                       padded(name, NAME_BYTES, "language name"))


def build_legacy_pack(code, catalogue, translations):
    """A version 1 pack, for an installed build that predates segments."""
    texts = pack_texts(code, catalogue, translations)
    block = text_block([texts[name] for name, _ in catalogue])
    size = HEADER.size + len(block)
    if size > PACK_LIMIT:
        raise PackError(f"{code}: the pack is {size:,} bytes, over the {PACK_LIMIT:,} byte limit")
    header = header_bytes(LEGACY_VERSION, code, texts[NAME_ID],
                          (len(catalogue), size, catalogue_hash(catalogue)))
    return sealed(bytearray(header + block), size)


def index_size(count):
    return HEADER.size + SEGMENT_INDEX.size + SEGMENT.size * count


def aligned(size):
    return (size + MEMORY_ALIGN - 1) // MEMORY_ALIGN * MEMORY_ALIGN


def memory_need(resident_end, module_sizes):
    """Window bytes for a pack: its resident block, aligned, then its largest module segment."""
    return aligned(resident_end) + max(module_sizes, default=0)


def build_pack(code, catalogue, translations, segments=None):
    segments = segments if segments is not None else resident_segments(catalogue)
    texts = pack_texts(code, catalogue, translations)
    blocks = [text_block([texts[name] for name in names]) for _, names in segments]
    offset = index_size(len(segments))
    table = bytearray(SEGMENT_INDEX.pack(len(segments), 0))
    for (module, names), block in zip(segments, blocks):
        table += SEGMENT.pack(module, len(names), offset, len(block), fnv(block))
        offset += len(block)
    need = memory_need(index_size(len(segments)) + len(blocks[0]),
                       [len(block) for block in blocks[1:]])
    if need > PACK_LIMIT:
        raise PackError(f"{code}: the pack needs {need:,} bytes of memory, "
                        f"over the {PACK_LIMIT:,} byte limit")
    if offset > FILE_LIMIT:
        raise PackError(f"{code}: the pack is {offset:,} bytes, over the {FILE_LIMIT:,} byte file limit")
    header = header_bytes(VERSION, code, texts[NAME_ID], (len(catalogue), offset, layout_hash(segments)))
    return sealed(bytearray(header + table + b"".join(blocks)), index_size(len(segments)))


def decoded_text(data, what):
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise PackError(f"{what} is not valid UTF-8") from error


def metadata_text(data, what):
    end = data.find(b"\0")
    if end <= 0 or any(data[end + 1:]):
        raise PackError(f"{what} must be nonempty, NUL-terminated and zero-padded")
    return decoded_text(data[:end], what)


def segment_total(pack):
    if len(pack) < HEADER.size + SEGMENT_INDEX.size:
        raise PackError("shorter than a pack index")
    total, reserved = SEGMENT_INDEX.unpack_from(pack, HEADER.size)
    if total == 0 or reserved or len(pack) < index_size(total):
        raise PackError("its segment index is damaged")
    return total


def pack_segments(pack, count):
    total = segment_total(pack)
    rows = [SEGMENT.unpack_from(pack, HEADER.size + SEGMENT_INDEX.size + SEGMENT.size * index)
            for index in range(total)]
    offset = index_size(total)
    for module, strings, start, size, checksum in rows:
        if start != offset or size <= 2 * strings or fnv(pack[start:start + size]) != checksum:
            raise PackError(f"its segment for module {module} is damaged")
        offset += size
    if offset != len(pack) or sum(row[1] for row in rows) != count:
        raise PackError("its segments do not cover the pack")
    if rows[0][0] != RESIDENT or any(row[0] == RESIDENT for row in rows[1:]):
        raise PackError("its resident segment must come first")
    return [{"module": module, "count": strings, "offset": start, "size": size}
            for module, strings, start, size, _ in rows]


def pack_info(pack):
    if len(pack) < HEADER.size:
        raise PackError("shorter than a pack header")
    magic, version, count, size, catalogue, checksum, code, name = HEADER.unpack_from(pack)
    if magic != MAGIC or version not in (VERSION, LEGACY_VERSION):
        raise PackError("not a version 1 or 2 language pack")
    limit = PACK_LIMIT if version == LEGACY_VERSION else FILE_LIMIT
    if size != len(pack) or size > limit or size <= HEADER.size + 2 * count:
        raise PackError(f"its size field is {size:,}, the file is {len(pack):,} bytes")
    end = size if version == LEGACY_VERSION else index_size(segment_total(pack))
    if checksum != fnv(pack[CHECKSUM_END:end], fnv(pack[:CHECKSUM_OFFSET])):
        raise PackError("its checksum is wrong")
    segments = pack_segments(pack, count) if version == VERSION else None
    language = metadata_text(code, "language code")
    if not CODE.fullmatch(language) or len(language) > CODE_LIMIT:
        raise PackError(f"language code must be 1 to {CODE_LIMIT} letters, digits or '-'")
    native_name = metadata_text(name, "language name")
    check_native_name(native_name)
    memory = size if segments is None else memory_need(
        segments[0]["offset"] + segments[0]["size"], [segment["size"] for segment in segments[1:]])
    return {"code": language, "name": native_name, "version": version, "count": count,
            "size": size, "memory": memory, "catalogue_hash": catalogue, "segments": segments}


def pack_code(pack):
    return pack_info(pack)["code"]


def pack_text(body, offset, name):
    if offset >= len(body) or (offset > 0 and body[offset - 1] != 0):
        raise PackError(f"{name}: its offset does not start a body string")
    end = body.find(b"\0", offset)
    if end < 0:
        raise PackError(f"{name}: its string is not NUL-terminated")
    text = decoded_text(body[offset:end], name)
    if not text:
        raise PackError(f"{name}: its string is empty")
    return text


def block_texts(block, names):
    body = block[2 * len(names):]
    if not body or body[-1] != 0:
        raise PackError("the pack body is not NUL-terminated")
    decoded_text(body, "pack body")
    offsets = struct.unpack_from(f"<{len(names)}H", block)
    return {name: pack_text(body, offset, name) for name, offset in zip(names, offsets)}


def pack_strings(pack, info, catalogue, segments):
    if info["version"] == LEGACY_VERSION:
        if info["catalogue_hash"] != catalogue_hash(catalogue):
            raise PackError("the pack catalogue does not match this firmware")
        return block_texts(pack[HEADER.size:], [name for name, _ in catalogue])
    shape = [(segment["module"], segment["count"]) for segment in info["segments"]]
    if info["catalogue_hash"] != layout_hash(segments) or shape != [
            (module, len(names)) for module, names in segments]:
        raise PackError("the pack catalogue does not match this firmware")
    texts = {}
    for segment, (_, names) in zip(info["segments"], segments):
        start = segment["offset"]
        texts.update(block_texts(pack[start:start + segment["size"]], names))
    return texts


def validate_pack(data, catalogue, drawable, segments=None, version=VERSION, limit=PACK_LIMIT):
    """Check a whole pack against a firmware's catalogue, segments, format and memory."""
    pack = bytes(data)
    info = pack_info(pack)
    if info["version"] != version:
        raise PackError(f"the firmware reads version {version} packs; "
                        f"this one is version {info['version']}")
    if info["count"] != len(catalogue):
        raise PackError("the pack catalogue does not match this firmware")
    if info["memory"] > limit:
        raise PackError(f"the pack needs {info['memory']:,} bytes of memory; the firmware has {limit:,}")
    segments = segments if segments is not None else resident_segments(catalogue)
    texts = pack_strings(pack, info, catalogue, segments)
    if texts.get(NAME_ID) != info["name"]:
        raise PackError("the header language name does not match its catalogue text")
    check_placeholders(info["code"], catalogue, texts)
    check_glyphs(info["code"], texts.items(), drawable)
    return {**info, "texts": texts}


def catalogue_info(catalogue, drawable, segments=None):
    segments = segments if segments is not None else resident_segments(catalogue)
    return {"version": VERSION, "magic": MAGIC.decode("ascii"), "pack_limit": PACK_LIMIT,
            "file_limit": FILE_LIMIT, "memory_align": MEMORY_ALIGN,
            "code_limit": CODE_LIMIT, "code_bytes": CODE_BYTES, "name_bytes": NAME_BYTES,
            "name_id": NAME_ID, "catalogue_hash": layout_hash(segments),
            "entries": [{"id": name, "english": text} for name, text in catalogue],
            "segments": [{"module": module, "ids": names} for module, names in segments],
            "glyphs": sorted(drawable)}


def browser_source(catalogue, drawable, segments=None):
    info = catalogue_info(catalogue, drawable, segments)
    return "globalThis.LANGUAGE_CATALOGUE = " + json.dumps(
        info, sort_keys=True, ensure_ascii=True, separators=(",", ":")) + ";\n"


def pseudo_word(match):
    word = match.group()
    for index, character in enumerate(word):
        accented = character.translate(ACCENTS)
        if accented != character:
            return word[:index] + accented + word[index + 1:]
    return word


def pseudo_text(text):
    """An accented, longer, bracketed stand-in that keeps the placeholders."""
    pieces = [piece if PLACEHOLDER.fullmatch(piece) else re.sub(r"[A-Za-z]+", pseudo_word, piece)
              for piece in PIECE.findall(text)]
    letters = sum(1 for character in PLACEHOLDER.sub("", text) if character.isalpha())
    return "[" + "".join(pieces) + "!" * ((letters + 2) // 3) + "]"


def pseudo_translations(catalogue):
    translations = {name: pseudo_text(text) for name, text in catalogue}
    translations[NAME_ID] = PSEUDO_NAME
    return translations


def language_pack(code, directory=LANGUAGES, fonts_directory=FONTS, layout=None):
    catalogue = read_catalogue(directory)
    segments = read_segments(catalogue, layout) if layout else string_segments(catalogue)
    if code == PSEUDO:
        translations = pseudo_translations(catalogue)
    elif code == ENGLISH:
        translations = {}
    else:
        translations = read_translations(Path(directory) / f"{code}.txt", catalogue)
    check_placeholders(code, catalogue, translations)
    texts = [(name, translations.get(name) or english) for name, english in catalogue]
    check_glyphs(code, texts, drawable_characters(fonts_directory))
    return build_pack(code, catalogue, translations, segments)


def c_literal(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"' + escaped + '"'


def header_source(catalogue, segments=None):
    segments = segments if segments is not None else resident_segments(catalogue)
    firsts = [0]
    for _, names in segments:
        firsts.append(firsts[-1] + len(names))
    modules = ", ".join(str(module) for module, _ in segments)
    lines = ["#pragma once", "", "#include <stdint.h>", "", "typedef enum StringId {"]
    lines += [f"    {name}," for name in ordered_names(segments)]
    lines += ["    STRING_COUNT", "} StringId;", "",
              f"#define STRINGS_HASH UINT32_C(0x{layout_hash(segments):08X})",
              f"#define STRING_SEGMENT_COUNT {len(segments)}",
              f"#define STRING_SEGMENT_MODULES {{{modules}}}",
              f"#define STRING_SEGMENT_FIRSTS {{{', '.join(str(first) for first in firsts)}}}", ""]
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


def generated_sources(catalogue, style_file, segments=None):
    return {HEADER_OUTPUT: formatted(header_source(catalogue, segments), style_file),
            ENGLISH_OUTPUT: formatted(english_source(catalogue), style_file)}


def write_sources(arguments):
    catalogue = read_catalogue(arguments.languages)
    outputs = generated_sources(catalogue, arguments.style, string_segments(catalogue))
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
    if stale:
        print("Regenerate with python3 tools/firmware.py regen from the repository root", file=sys.stderr)
    return 1 if stale else 0


def write_pack(arguments):
    pack = language_pack(arguments.pack, arguments.languages, layout=arguments.layout)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(pack)
    info = pack_info(pack)
    print(f"{arguments.pack}: {info['memory']:,} of {PACK_LIMIT:,} memory bytes, {len(pack):,} file bytes")
    return 0


def write_catalogue(arguments):
    catalogue = read_catalogue(arguments.languages)
    source = browser_source(catalogue, drawable_characters(), string_segments(catalogue))
    path = arguments.export_catalogue
    if arguments.check:
        if not path.exists() or path.read_text(encoding="utf-8") != source:
            print(f"out of date: {path}", file=sys.stderr)
            print("Regenerate with python3 tools/firmware.py regen from the repository root", file=sys.stderr)
            return 1
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
    return 0


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Build language packs and the string catalogue")
    parser.add_argument("--languages", type=Path, default=LANGUAGES,
                        help="folder with en.txt and the translations")
    parser.add_argument("--sources", type=Path, help="src/core: write the generated C headers there")
    parser.add_argument("--style", type=Path, help=".clang-format for the generated headers")
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed headers differ from a fresh build")
    parser.add_argument("--export-catalogue", type=Path,
                        help="write the browser catalogue and drawable glyphs as JavaScript")
    parser.add_argument("--pack", help=f"language code to pack; {PSEUDO} is the test language")
    parser.add_argument("--output", type=Path, help="pack file to write")
    parser.add_argument("--layout", type=Path,
                        help="generated strings.h whose string segments the pack follows")
    arguments = parser.parse_args(argv)
    if bool(arguments.pack) != bool(arguments.output):
        parser.error("--pack and --output go together")
    if not arguments.pack and not arguments.sources and not arguments.export_catalogue:
        parser.error("nothing to do: give --sources, --pack or --export-catalogue")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        status = write_sources(arguments) if arguments.sources else 0
        status = status or (write_pack(arguments) if arguments.pack else 0)
        return status or (write_catalogue(arguments) if arguments.export_catalogue else 0)
    except PackError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
