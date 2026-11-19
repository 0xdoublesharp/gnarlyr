"""Checks the echo_device_logic files that join the core 0 link-time optimisation.

Those files are built as fat LTO objects: the main program links their intermediate code,
everything else links their regular code. That is safe only while each of them
- is linked into no core 1 program,
- holds no variables, so the main program and an overlay never keep two copies of one state,
- is reached by the rescan and USB overlays through the main program's copy only for symbols
  the main program exports anyway (ECHO_OVERLAY_IMPORTS or a module import list), which the
  linker keeps global with their normal calling convention; anything else link-time
  optimisation may inline, specialise or drop, so an overlay must use its own copy.
The image build runs this check and stops on any violation.
"""
import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ELF_IDENT = b"\x7fELF\x01\x01"
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
ELF_SECTION = struct.Struct("<10I")
ELF_SYMBOL = struct.Struct("<IIIBBH")
SHT_SYMTAB = 2
SHT_NOBITS = 8
SHF_ALLOC = 2
SHN_UNDEF = 0
SHN_COMMON = 0xFFF2
SHN_LORESERVE = 0xFF00
STB_GLOBAL = 1
STB_WEAK = 2
STT_OBJECT = 1
STT_FUNC = 2
BRANCH_LINK = 0xD000
BRANCH_WIDE = 0x9000
STATE_PREFIXES = (".data", ".bss", ".noinit", ".tdata", ".tbss")
ARCHIVE = "libecho_device_logic.a"


@dataclass(frozen=True)
class Section:
    name: str
    kind: int
    flags: int
    address: int
    content: bytes
    size: int


@dataclass(frozen=True)
class Symbol:
    name: str
    value: int
    size: int
    binding: int
    kind: int
    section: int


@dataclass(frozen=True)
class Elf:
    sections: tuple
    symbols: tuple


def read_sections(data):
    fields = ELF_HEADER.unpack_from(data)
    table, count, names_index = fields[6], fields[12], fields[13]
    raw = [ELF_SECTION.unpack_from(data, table + index * ELF_SECTION.size)
           for index in range(count)]
    names = raw[names_index] if raw else None
    sections = []
    for name, kind, flags, address, offset, size, *_ in raw:
        start = names[4] + name
        label = data[start:data.index(b"\x00", start)].decode("ascii", "replace")
        content = b"" if kind == SHT_NOBITS else data[offset:offset + size]
        sections.append((Section(label, kind, flags, address, content, size), raw[len(sections)]))
    return sections


def read_symbols(data, sections):
    for section, raw in sections:
        if section.kind == SHT_SYMTAB:
            strings = sections[raw[6]][0].content
            return tuple(symbol_at(section.content, offset, strings)
                         for offset in range(0, len(section.content), ELF_SYMBOL.size))
    return ()


def symbol_at(table, offset, strings):
    name, value, size, info, _, index = ELF_SYMBOL.unpack_from(table, offset)
    label = strings[name:strings.index(b"\x00", name)].decode("ascii", "replace")
    return Symbol(label, value, size, info >> 4, info & 0xF, index)


def read_elf(path):
    data = Path(path).read_bytes()
    if data[:len(ELF_IDENT)] != ELF_IDENT or len(data) < ELF_HEADER.size:
        raise ValueError(f"{path} is not a 32-bit little-endian ELF file")
    sections = read_sections(data)
    return Elf(tuple(section for section, _ in sections), read_symbols(data, sections))


def core1_links(member, maps):
    pattern = re.compile(re.escape(ARCHIVE) + r"\(" + re.escape(member) + r"\)")
    return sorted(name for name, text in maps.items() if pattern.search(text))


def state_symbols(elf):
    state = {index: section.name for index, section in enumerate(elf.sections)
             if section.size and section.name.startswith(STATE_PREFIXES)}
    named = {symbol.name for symbol in elf.symbols
             if symbol.section == SHN_COMMON
             or (symbol.section in state and symbol.kind == STT_OBJECT)}
    covered = {symbol.section for symbol in elf.symbols
               if symbol.section in state and symbol.kind == STT_OBJECT}
    return sorted(named | {name for index, name in state.items() if index not in covered})


def defined_names(elf):
    return {symbol.name for symbol in elf.symbols
            if symbol.binding in (STB_GLOBAL, STB_WEAK) and symbol.kind in (STT_OBJECT, STT_FUNC)
            and SHN_UNDEF < symbol.section < SHN_LORESERVE}


def main_addresses(main, names):
    return {symbol.value & ~1: symbol.name for symbol in main.symbols
            if symbol.name in names and symbol.kind in (STT_OBJECT, STT_FUNC)
            and SHN_UNDEF < symbol.section < SHN_LORESERVE}


def branch_target(address, first, second):
    if first & 0xF800 != 0xF000 or second & 0xD000 not in (BRANCH_LINK, BRANCH_WIDE):
        return None
    sign = (first >> 10) & 1
    i1 = 1 ^ (((second >> 13) & 1) ^ sign)
    i2 = 1 ^ (((second >> 11) & 1) ^ sign)
    offset = (i1 << 23) | (i2 << 22) | ((first & 0x3FF) << 12) | ((second & 0x7FF) << 1)
    offset -= sign << 24
    return (address + 4 + offset) & 0xFFFFFFFF


def references(section):
    found = set()
    content = section.content
    for offset in range(0, len(content) - 3, 2):
        word = struct.unpack_from("<I", content, offset)[0]
        found.add(word & ~1)
        first, second = struct.unpack_from("<HH", content, offset)
        target = branch_target(section.address + offset, first, second)
        if target is not None:
            found.add(target)
    return found


def overlay_uses(overlay, addresses):
    used = set()
    for section in overlay.sections:
        if section.flags & SHF_ALLOC and section.kind != SHT_NOBITS and section.content:
            used |= {addresses[target] for target in references(section) if target in addresses}
    return sorted(used)


def hidden_uses(overlay, addresses, exported):
    return [symbol for symbol in overlay_uses(overlay, addresses) if symbol not in exported]


def read_exported(paths):
    names = set()
    for path in paths:
        for token in Path(path).read_text(encoding="utf-8").split():
            names.add(token.rpartition("=")[2])
    return names


def problems_for(path, maps, main, overlays, exported):
    member = Path(path).name
    elf = read_elf(path)
    found = []
    linked = core1_links(member, maps)
    if linked:
        found.append(f"{member} is linked into {', '.join(linked)}")
    state = state_symbols(elf)
    if state:
        found.append(f"{member} holds variables: {', '.join(state)}")
    addresses = main_addresses(main, defined_names(elf))
    for name, overlay in overlays.items():
        hidden = hidden_uses(overlay, addresses, exported)
        if hidden:
            found.append(f"{name} uses the main program's {', '.join(hidden)} from {member}, "
                         "which no import list exports")
    return found


def lto_objects(objects, sources):
    by_name = {Path(path).name: path for path in objects}
    missing = [source for source in sources if f"{source}.obj" not in by_name]
    if missing:
        raise ValueError("no object for " + ", ".join(missing))
    return [by_name[f"{source}.obj"] for source in sources]


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--main", type=Path, required=True, help="the main program ELF")
    parser.add_argument("--objects", type=Path, required=True,
                        help="a file listing every echo_device_logic object, one per line")
    parser.add_argument("--lto-source", action="append", default=[],
                        help="an echo_device_logic source built for link-time optimisation")
    parser.add_argument("--core1-map", type=Path, action="append", default=[])
    parser.add_argument("--overlay", type=Path, action="append", default=[])
    parser.add_argument("--imports", type=Path, action="append", default=[],
                        help="a list of symbols the main program exports (NAME or --opt=NAME)")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        objects = arguments.objects.read_text(encoding="utf-8").split()
        paths = lto_objects(objects, arguments.lto_source)
        maps = {path.stem: path.read_text(encoding="utf-8", errors="replace")
                for path in arguments.core1_map}
        program = read_elf(arguments.main)
        overlays = {path.stem: read_elf(path) for path in arguments.overlay}
        exported = read_exported(arguments.imports)
        found = [problem for path in paths
                 for problem in problems_for(path, maps, program, overlays, exported)]
    except (OSError, ValueError) as error:
        print("error: " + str(error), file=sys.stderr)
        return 1
    for problem in found:
        print("error: link-time optimised device logic: " + problem, file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
