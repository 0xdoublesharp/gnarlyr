import argparse
import struct
from pathlib import Path


def upcase(fixture):
    data = fixture.read_bytes()
    sectors = {struct.unpack_from("<I", data, offset)[0]: data[offset + 4:offset + 516]
               for offset in range(12, len(data), 516)}
    start = struct.unpack_from("<I", sectors[0], 454)[0]
    boot = sectors[start]
    heap = start + struct.unpack_from("<I", boot, 88)[0]
    shift = boot[109]
    root = struct.unpack_from("<I", boot, 96)[0]
    directory = sectors[heap + ((root - 2) << shift)]
    entry = next(directory[offset:offset + 32] for offset in range(0, 512, 32)
                 if directory[offset] == 0x82)
    cluster, length = struct.unpack_from("<IQ", entry, 20)
    first = heap + ((cluster - 2) << shift)
    table = b"".join(sectors.get(first + index, bytes(512))
                     for index in range((length + 511) // 512))[:length]
    checksum = 0
    for byte in table:
        checksum = ((checksum >> 1) | (checksum << 31)) + byte
        checksum &= 0xFFFFFFFF
    if checksum != 0xE619D30D or length != 5836:
        raise ValueError("fixture does not contain the recommended exFAT up-case table")
    return table


def source(table):
    lines = ["#pragma once", "", "#include <stdint.h>", "",
             "static const uint8_t CARD_FORMAT_UPCASE[] = {"]
    for offset in range(0, len(table), 16):
        lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in table[offset:offset + 16]) + ",")
    return "\n".join([*lines, "};", ""])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    text = source(upcase(root / "tests/fixtures/cards/exfat_mbr.sparse"))
    if args.check:
        if args.output.read_text() != text:
            raise SystemExit("card formatting table is out of date")
    else:
        args.output.write_text(text)


if __name__ == "__main__":
    main()
