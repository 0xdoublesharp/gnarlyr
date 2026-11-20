"""Runs `nm` and reads its listings, with or without `-S` sizes and `-l` source lines.

The default form is `[address] [size] kind name[<TAB>file:line]`; the address and size are
zero-padded hexadecimal, so the first single-character field is the kind. The System V form
(`-f sysv`) is `name|value|class|type|size|line|section[<TAB>file:line]` and adds the ELF symbol
type, which tells functions from data. Headers, archive member names, lines without a name and
other text are skipped.
"""
import subprocess
from dataclasses import dataclass

DEFAULT_NM = "arm-none-eabi-nm"


@dataclass(frozen=True)
class Symbol:
    address: int | None
    size: int | None
    kind: str
    name: str
    file: str | None = None
    line: int | None = None
    type: str | None = None


def source_location(text):
    if not text:
        return None, None
    file, separator, line = text.rpartition(":")
    if separator and line.isdigit():
        return file, int(line)
    return text, None


def parse_sysv(text, location):
    fields = [field.strip() for field in text.split("|")]
    if len(fields) != 7 or not fields[0]:
        return None
    try:
        address = int(fields[1], 16)
        size = int(fields[4], 16) if fields[4] else None
    except ValueError:
        return None
    file, number = source_location(location)
    return Symbol(address, size, fields[2], fields[0], file, number, fields[3] or None)


def parse_line(line):
    text, _, location = line.partition("\t")
    if "|" in text:
        return parse_sysv(text, location)
    fields = text.split()
    if not fields or fields[0].endswith(":"):
        return None
    index = next((index for index, field in enumerate(fields) if len(field) == 1), None)
    if index is None or index > 2 or len(fields) != index + 2:
        return None
    try:
        numbers = [int(field, 16) for field in fields[:index]]
    except ValueError:
        return None
    file, number = source_location(location)
    return Symbol(numbers[0] if numbers else None, numbers[1] if len(numbers) == 2 else None,
                  fields[index], fields[index + 1], file, number)


def parse(text):
    return [symbol for symbol in map(parse_line, text.splitlines()) if symbol is not None]


def run(nm, path, *options, check=False):
    return subprocess.run([nm, *options, str(path)], capture_output=True, text=True, check=check)
