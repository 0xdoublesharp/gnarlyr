#!/usr/bin/env python3
"""Read-only RKnano module inspection and byte-preserving ELF analysis export.

The ELF is an analysis representation, not runnable or flashable firmware.
Module/CRC layout references are recorded in docs/initial-investigation.md.
"""
from __future__ import annotations
import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import struct

POLYNOMIAL = 0x04C10DB7
FIELDS = ('code_load', 'code_address', 'code_size', 'data_load',
          'data_address', 'data_size', 'bss_address', 'bss_size')
SECTIONS = {'sysdefault': 0x70, 'font_a': 0x78, 'font_b': 0x80,
            'codepage': 0xCC, 'language': 0xF8, 'images': 0x14C}


def rockchip_crc(data: bytes) -> int:
    table = []
    for byte in range(256):
        value = byte << 24
        for _ in range(8):
            value = ((value << 1) ^ (POLYNOMIAL if value & 0x80000000 else 0)) & 0xFFFFFFFF
        table.append(value)
    value = 0
    for byte in data:
        value = ((value << 8) & 0xFFFFFFFF) ^ table[((value >> 24) ^ byte) & 255]
    return value


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]


@dataclass(frozen=True)
class Region:
    name: str
    address: int
    size: int
    file_offset: int | None
    executable: bool = False


class Image:
    def __init__(self, data: bytes):
        if not 0x208 <= len(data) <= 128 * 1024 * 1024:
            raise ValueError('Image must be 0x208 bytes to 128 MiB')
        if data[0x10:0x18] != b'Rockchip' or data[0x1F8:0x200] != b'RKnanoFW':
            raise ValueError('Not a recognized RKnanoFW container')
        self.data = data
        self.digest = hashlib.sha256(data).hexdigest()
        self.crc = u32(data, len(data) - 4)
        if rockchip_crc(data[:-4]) != self.crc:
            raise ValueError('Rockchip trailer CRC mismatch')
        self.load_base, count = struct.unpack_from('<II', data, 0x200)
        if not count or 0x208 + count * 32 > len(data) - 4:
            raise ValueError('Invalid module table extent')
        self.modules = []
        for index in range(count):
            row = dict(zip(FIELDS, struct.unpack_from('<8I', data, 0x208 + index * 32)))
            row['index'] = index
            for kind in ('code', 'data'):
                offset = row[kind + '_load'] - self.load_base + 0x200
                row[kind + '_file_offset'] = offset
                size = row[kind + '_size']
                if size and (offset < 0 or offset + size > len(data) - 4):
                    raise ValueError(f'Module {index} {kind} exceeds image')
                if row[kind + '_address'] + size > 0x100000000:
                    raise ValueError(f'Module {index} {kind} exceeds ARM32 address space')
            if row['bss_address'] + row['bss_size'] > 0x100000000:
                raise ValueError(f'Module {index} BSS exceeds ARM32 address space')
            self.modules.append(row)
        if self.modules[0]['code_size'] < 64:
            raise ValueError('Main module lacks the architectural vector table')
        self.sections = {}
        for name, field in SECTIONS.items():
            offset, size = struct.unpack_from('<II', data, field)
            if size and offset + size > len(data) - 4:
                raise ValueError(f'Resource {name} exceeds image')
            self.sections[name] = {'file_offset': offset, 'size': size}

    def summary(self) -> dict:
        module = self.modules[0]
        reset = u32(self.data, module['code_file_offset'] + 4)
        return {'sha256': self.digest, 'size': len(self.data), 'crc32': f'0x{self.crc:08x}',
                'load_base': self.load_base, 'module_count': len(self.modules),
                'main_initial_sp': u32(self.data, module['code_file_offset']),
                'main_reset_vector': reset, 'resources': self.sections, 'modules': self.modules}

    def regions(self, selected: list[int]) -> list[Region]:
        if not selected or len(selected) != len(set(selected)):
            raise ValueError('Select one or more distinct modules')
        regions = []
        for index in selected:
            if index < 0 or index >= len(self.modules):
                raise ValueError(f'No module {index}')
            row = self.modules[index]
            for kind in ('code', 'data', 'bss'):
                size = row[kind + '_size']
                if size:
                    regions.append(Region(f'm{index:03d}_{kind}', row[kind + '_address'], size,
                                          row[kind + '_file_offset'] if kind != 'bss' else None,
                                          kind == 'code'))
        for i, a in enumerate(regions):
            for b in regions[i+1:]:
                if max(a.address, b.address) < min(a.address + a.size, b.address + b.size):
                    raise ValueError(f'Overlapping execution regions: {a.name}, {b.name}; export separately')
        return regions

    def analysis_elf(self, selected: list[int]) -> tuple[bytes, dict]:
        if 0 not in selected:
            raise ValueError('Include main module 0 to retain its vector table and entry')
        regions = self.regions(selected)
        main = self.modules[0]
        entry = u32(self.data, main['code_file_offset'] + 4)
        if not entry & 1 or not main['code_address'] <= entry - 1 < main['code_address'] + main['code_size']:
            raise ValueError('Reset vector is not a Thumb entry inside main code')
        # ELF32, little endian, ET_EXEC, EM_ARM, EABI5 (analysis convention).
        headers_size = 52 + 32 * len(regions)
        output = bytearray(headers_size)
        loads, sections, mapping = [], [], []
        for region in regions:
            output.extend(b'\0' * (-len(output) % 4))
            offset = len(output)
            payload = b'' if region.file_offset is None else self.data[region.file_offset:region.file_offset+region.size]
            output.extend(payload)
            flags = 5 if region.executable else 6
            loads.append((1, offset, region.address, region.address, len(payload), region.size, flags, 4))
            sections.append(('.' + region.name, 8 if region.file_offset is None else 1,
                             6 if region.executable else 3, region.address, offset, region.size, 0, 0, 4, 0))
            mapping.append({**asdict(region), 'elf_file_offset': offset,
                            'sha256': hashlib.sha256(payload).hexdigest() if payload else None})
        # Known vector role, not a recovered original symbol name.
        strings = b'\0reset_handler\0'
        output.extend(b'\0' * (-len(output) % 4))
        symbols_offset = len(output)
        main_index = next(i+1 for i, r in enumerate(regions) if r.name == 'm000_code')
        output.extend(bytes(16) + struct.pack('<IIIBBH', 1, entry, 0, 0x12, 0, main_index))
        strtab_offset = len(output)
        output.extend(strings)
        symtab_index = len(sections) + 1
        strtab_index = symtab_index + 1
        sections.append(('.symtab', 2, 0, 0, symbols_offset, 32, strtab_index, 1, 4, 16))
        sections.append(('.strtab', 3, 0, 0, strtab_offset, len(strings), 0, 0, 1, 0))
        names = bytearray(b'\0')
        named = []
        for section in sections:
            name_offset = len(names)
            names.extend(section[0].encode() + b'\0')
            named.append((name_offset, *section[1:]))
        name_offset = len(names)
        names.extend(b'.shstrtab\0')
        name_data_offset = len(output)
        output.extend(names)
        named.append((name_offset, 3, 0, 0, name_data_offset, len(names), 0, 0, 1, 0))
        output.extend(b'\0' * (-len(output) % 4))
        section_offset = len(output)
        output.extend(bytes(40))
        for section in named:
            output.extend(struct.pack('<10I', *section))
        ident = b'\x7fELF\x01\x01\x01' + bytes(9)
        struct.pack_into('<16sHHIIIIIHHHHHH', output, 0, ident, 2, 40, 1, entry,
                         52, section_offset, 0x05000000, 52, 32, len(loads), 40, len(named)+1, len(named))
        for index, load in enumerate(loads):
            struct.pack_into('<8I', output, 52 + index * 32, *load)
        result = bytes(output)
        # Independently read PT_LOAD rows and verify every initialized byte.
        for index, region in enumerate(regions):
            _, off, addr, _, filesz, memsz, _, _ = struct.unpack_from('<8I', result, 52+32*index)
            if addr != region.address or memsz != region.size:
                raise ValueError('ELF memory map readback failed')
            expected = b'' if region.file_offset is None else self.data[region.file_offset:region.file_offset+region.size]
            if result[off:off+filesz] != expected:
                raise ValueError('ELF payload readback failed')
        return result, {'purpose': 'analysis-only; never flash this ELF', 'source_sha256': self.digest,
                        'elf_sha256': hashlib.sha256(result).hexdigest(), 'selected_modules': selected,
                        'language': 'ARM:LE:32:Cortex', 'thumb_entry': entry, 'regions': mapping}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--modules', help='Export selected non-overlapping modules, e.g. 0,1')
    parser.add_argument('--output', type=Path, help='New ELF path on Scratch')
    args = parser.parse_args()
    image = Image(args.image.read_bytes())
    if args.modules:
        if not args.output:
            parser.error('--modules requires --output')
        selected = [int(v) for v in args.modules.split(',')]
        elf, mapping = image.analysis_elf(selected)
        map_path = args.output.with_suffix('.map.json')
        if args.output.exists() or map_path.exists():
            parser.error('Output already exists; choose a new path')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('xb') as stream:
            stream.write(elf)
        with map_path.open('x') as stream:
            json.dump(mapping, stream, indent=2)
            stream.write('\n')
        print(json.dumps(mapping, indent=2))
    else:
        print(json.dumps(image.summary(), indent=2))


if __name__ == '__main__':
    main()
