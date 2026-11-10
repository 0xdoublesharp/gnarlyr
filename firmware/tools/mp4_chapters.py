from dataclasses import dataclass
from typing import NamedTuple

MAX_CHAPTERS = 255
MAX_SAMPLES = 1024
MAX_TIME_MS = 0xFFFFFFFF
TITLE_BYTES = 63


class Chapter(NamedTuple):
    start_ms: int
    title: str


class InvalidChapters(ValueError):
    pass


@dataclass(frozen=True)
class Atom:
    kind: bytes
    body: int
    end: int


class Reader:
    def __init__(self, handle, size):
        self.handle = handle
        self.size = size

    def read(self, offset, length, end=None):
        limit = self.size if end is None else min(end, self.size)
        if offset < 0 or length < 0 or offset > limit or length > limit - offset:
            raise InvalidChapters("chapter data exceeds its container")
        self.handle.seek(offset)
        data = self.handle.read(length)
        if len(data) != length:
            raise InvalidChapters("chapter data is truncated")
        return data

    def number(self, atom, offset, width=4):
        return int.from_bytes(self.read(atom.body + offset, width, atom.end), "big")

    def children(self, parent):
        offset = parent.body
        while offset < parent.end:
            header = self.read(offset, 8, parent.end)
            length = int.from_bytes(header[:4], "big")
            body = offset + 8
            if length == 1:
                length = int.from_bytes(self.read(body, 8, parent.end), "big")
                body += 8
            elif length == 0:
                length = parent.end - offset
            if length < body - offset or length > parent.end - offset:
                raise InvalidChapters("invalid chapter atom size")
            yield Atom(header[4:], body, offset + length)
            offset += length

    def child(self, parent, *path):
        for kind in path:
            parent = next((atom for atom in self.children(parent) if atom.kind == kind), None)
            if parent is None:
                raise InvalidChapters("chapter atom is missing")
        return parent

    def table(self, atom, width):
        count = self.number(atom, 4)
        if count * width > atom.end - atom.body - 8:
            raise InvalidChapters("chapter table is truncated")
        return count


def title_text(raw):
    if raw.startswith((b"\xfe\xff", b"\xff\xfe")):
        return ""
    return raw.split(b"\0", 1)[0][:TITLE_BYTES].decode("utf-8", errors="ignore")


def append_chapter(chapters, start, title, duration):
    if len(chapters) >= MAX_CHAPTERS or (duration and start >= duration):
        return
    if chapters and start <= chapters[-1].start_ms:
        return
    chapters.append(Chapter(start if chapters else 0, title or f"Chapter {len(chapters) + 1}"))


def chpl_entries(reader, atom, skip, duration):
    count = reader.number(atom, skip, 1)
    cursor, chapters = atom.body + skip + 1, []
    for _ in range(count):
        row = reader.read(cursor, 9, atom.end)
        start = int.from_bytes(row[:8], "big") // 10000
        length = row[8]
        if start > MAX_TIME_MS:
            raise InvalidChapters("chapter time exceeds the player limit")
        raw = reader.read(cursor + 9, length, atom.end)
        append_chapter(chapters, start, title_text(raw), duration)
        cursor += 9 + length
    return chapters


def read_chpl(reader, moov, duration):
    atom = reader.child(moov, b"udta", b"chpl")
    for skip in (8, 4):
        try:
            chapters = chpl_entries(reader, atom, skip, duration)
            if len(chapters) >= 2:
                return chapters
        except InvalidChapters:
            pass
    return []


def chapter_track(reader, moov):
    wanted = None
    for track in (atom for atom in reader.children(moov) if atom.kind == b"trak"):
        try:
            wanted = reader.number(reader.child(track, b"tref", b"chap"), 0)
            break
        except InvalidChapters:
            pass
    for track in (atom for atom in reader.children(moov) if atom.kind == b"trak"):
        try:
            header = reader.child(track, b"tkhd")
            version = reader.number(header, 0, 1)
            if version in (0, 1) and reader.number(header, 20 if version else 12) == wanted:
                return track
        except InvalidChapters:
            pass
    raise InvalidChapters("referenced chapter track is missing")


def time_values(reader, table, count):
    runs, time, emitted = reader.table(table, 8), 0, 0
    for run in range(runs):
        amount = reader.number(table, 8 + run * 8)
        delta = reader.number(table, 12 + run * 8)
        for _ in range(min(amount, count - emitted)):
            yield time
            time += delta
            emitted += 1
        if emitted == count:
            return
    if emitted < count:
        raise InvalidChapters("chapter timing does not cover the samples")


def sample_places(reader, tables, count):
    sizes = reader.child(tables, b"stsz")
    fixed, total = reader.number(sizes, 4), reader.number(sizes, 8)
    if total < count or (not fixed and total * 4 > sizes.end - sizes.body - 12):
        raise InvalidChapters("chapter sizes do not cover the samples")
    mapping = reader.child(tables, b"stsc")
    runs = reader.table(mapping, 12)
    try:
        chunks, width = reader.child(tables, b"stco"), 4
    except InvalidChapters:
        chunks, width = reader.child(tables, b"co64"), 8
    chunk_count, run, sample = reader.table(chunks, width), 0, 0
    if not runs or reader.number(mapping, 8) != 1:
        raise InvalidChapters("chapter chunks have no first mapping")
    for chunk in range(1, chunk_count + 1):
        while run + 1 < runs and reader.number(mapping, 8 + (run + 1) * 12) <= chunk:
            run += 1
        amount = reader.number(mapping, 12 + run * 12)
        if not amount:
            raise InvalidChapters("chapter chunk has no samples")
        offset = reader.number(chunks, 8 + (chunk - 1) * width, width)
        if offset > MAX_TIME_MS:
            raise InvalidChapters("chapter offset exceeds the player limit")
        for _ in range(min(amount, count - sample)):
            length = fixed or reader.number(sizes, 12 + sample * 4)
            if offset > reader.size or length > reader.size - offset:
                raise InvalidChapters("chapter sample exceeds the file")
            yield offset, length
            offset += length
            sample += 1
        if sample == count:
            return
    if sample < count:
        raise InvalidChapters("chapter chunks do not cover the samples")


def read_text_track(reader, moov, duration):
    media = reader.child(chapter_track(reader, moov), b"mdia")
    header = reader.child(media, b"mdhd")
    version = reader.number(header, 0, 1)
    if version not in (0, 1):
        raise InvalidChapters("unknown chapter time header")
    scale = reader.number(header, 20 if version else 12)
    if not scale:
        raise InvalidChapters("chapter timescale is zero")
    tables = reader.child(media, b"minf", b"stbl")
    sizes = reader.child(tables, b"stsz")
    count = min(reader.number(sizes, 8), MAX_SAMPLES)
    times = time_values(reader, reader.child(tables, b"stts"), count)
    chapters = []
    for time, (offset, length) in zip(times, sample_places(reader, tables, count), strict=True):
        raw = reader.read(offset, min(length, TITLE_BYTES + 3))
        declared = int.from_bytes(raw[:2], "big") if length >= 2 else 0
        title = title_text(raw[2:2 + min(declared, max(length - 2, 0))])
        append_chapter(chapters, min(time * 1000 // scale, MAX_TIME_MS), title, duration)
        if len(chapters) == MAX_CHAPTERS:
            break
    return chapters if len(chapters) >= 2 else []


def read_chapters(handle, size, duration_ms):
    reader = Reader(handle, size)
    try:
        moov = reader.child(Atom(b"", 0, size), b"moov")
    except InvalidChapters:
        return []
    for parse in (read_chpl, read_text_track):
        try:
            chapters = parse(reader, moov, duration_ms)
            if chapters:
                return chapters
        except InvalidChapters:
            pass
    return []
