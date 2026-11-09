import argparse
import struct
import sys
from pathlib import Path

CHAPTER_UNITS = 10000
TAG_TEXT = {
    "\xa9nam": "Book",
    "\xa9ART": "Reader",
    "aART": "Writer",
    "\xa9alb": "Book",
    "\xa9gen": "Audiobook",
    "\xa9day": "2020",
}


def box(kind, *parts):
    body = b"".join(parts)
    return struct.pack(">I4s", 8 + len(body), kind) + body


def full(kind, *parts, version=0, flags=0):
    return box(kind, struct.pack(">I", version << 24 | flags), *parts)


def tkhd(track_id):
    return full("tkhd".encode(), struct.pack(">IIIII", 0, 0, track_id, 0, 0), bytes(60), flags=3)


def mdhd(timescale, duration):
    return full("mdhd".encode(), struct.pack(">IIIIHH", 0, 0, timescale, duration, 0x55C4, 0))


def hdlr(handler):
    return full("hdlr".encode(), struct.pack(">I4s", 0, handler), bytes(12), b"\0")


def table(kind, count, rows):
    return full(kind, struct.pack(">I", count), *rows)


def tag_items(title):
    items = []
    for kind, text in TAG_TEXT.items():
        value = title if kind in ("\xa9nam", "\xa9alb") else text
        data = full("data".encode(), struct.pack(">I", 0), value.encode(), flags=1)
        items.append(box(kind.encode("latin-1"), data))
    return box("ilst".encode(), *items)


def meta(title):
    return full("meta".encode(), box("hdlr".encode(), bytes(8), "mdir".encode(), bytes(12)),
                tag_items(title))


def chpl(entries, reserved=True, declared=None):
    rows = b"".join(struct.pack(">QB", start * CHAPTER_UNITS, len(name.encode())) + name.encode()
                    for start, name in entries)
    return full("chpl".encode(), bytes(4) if reserved else b"", struct.pack(">B", declared or len(entries)),
                rows)


def audio_track(duration, chapter_track=None):
    parts = [tkhd(1)]
    if chapter_track:
        parts.append(box("tref".encode(), box("chap".encode(), struct.pack(">I", chapter_track))))
    parts.append(box("mdia".encode(), mdhd(1000, duration), hdlr("soun".encode())))
    return box("trak".encode(), *parts)


def text_samples(titles):
    return [struct.pack(">H", len(text.encode())) + text.encode() for text in titles]


def text_track(timescale, deltas, sizes, per_chunk, chunk_offsets):
    stts = table("stts".encode(), len(deltas), [struct.pack(">II", 1, delta) for delta in deltas])
    stsz = full("stsz".encode(), struct.pack(">II", 0, len(sizes)),
                *[struct.pack(">I", size) for size in sizes])
    stsc = table("stsc".encode(), len(per_chunk),
                 [struct.pack(">III", first, count, 1) for first, count in per_chunk])
    stco = table("stco".encode(), len(chunk_offsets), [struct.pack(">I", at) for at in chunk_offsets])
    stbl = box("stbl".encode(), stts, stsc, stsz, stco)
    mdia = box("mdia".encode(), mdhd(timescale, sum(deltas)), hdlr("text".encode()),
               box("minf".encode(), stbl))
    return box("trak".encode(), tkhd(2), mdia)


def assemble(builder, payload=b"\x00" * 64):
    ftyp = box("ftyp".encode(), b"M4B ", struct.pack(">I", 0), b"M4B isom")
    mdat = box("mdat".encode(), payload)
    base = len(ftyp) + 8
    moov = box("moov".encode(), *builder(base))
    return ftyp + mdat + moov


def with_chpl(title, duration, entries, reserved=True, declared=None):
    def build(_base):
        udta = box("udta".encode(), meta(title), chpl(entries, reserved, declared))
        return [audio_track(duration), udta]
    return assemble(build)


def plain(title, duration):
    def build(_base):
        return [audio_track(duration), box("udta".encode(), meta(title))]
    return assemble(build)


def with_text_track(title, titles, seconds, offset_shift=0):
    samples = text_samples(titles)
    sizes = [len(sample) for sample in samples]
    deltas = [seconds_in * 44100 for seconds_in in seconds]
    first = 3 if len(samples) > 3 else len(samples)
    per_chunk = [(1, first)] + ([(2, 1)] if len(samples) > first else [])

    def build(base):
        second = base + sum(sizes[:first]) + 16 + offset_shift
        offsets = [base + offset_shift] + ([second] if len(samples) > first else [])
        track = text_track(44100, deltas, sizes, per_chunk, offsets)
        return [audio_track(sum(seconds) * 1000, chapter_track=2), track,
                box("udta".encode(), meta(title))]

    payload = b"".join(samples[:first]) + bytes(16) + b"".join(samples[first:]) + bytes(16)
    return assemble(build, payload)


BOOKS = {
    "Chpl Book.m4b": lambda: with_chpl(
        "Chpl Book", 300000, [(0, "Opening"), (100000, "The Middle"), (220000, "Finale")]),
    "Nero Book.m4b": lambda: with_chpl(
        "Nero Book", 120000, [(0, "Part One"), (50000, "Part Two")], reserved=False),
    "Track Book.m4b": lambda: with_text_track(
        "Track Book", ["One", "Two", "Three", "Four"], [60, 60, 90, 90]),
    "Plain Book.m4b": lambda: plain("Plain Book", 90000),
    "Single Chapter.m4b": lambda: with_chpl("Single Chapter", 90000, [(0, "Only")]),
    "Broken Chpl.m4b": lambda: with_chpl(
        "Broken Chpl", 90000, [(0, "A"), (40000, "B")], declared=5),
    "Broken Track.m4b": lambda: with_text_track(
        "Broken Track", ["One", "Two"], [60, 60], offset_shift=100000),
    "Messy Chpl.m4b": lambda: with_chpl(
        "Messy Chpl", 300000,
        [(0, "A"), (0, "A again"), (100000, "\xe9" * 35), (90000, "back"), (150000, ""),
         (900000, "late")]),
}


def write_tree(output):
    folder = output / "Audiobooks"
    folder.mkdir(parents=True, exist_ok=True)
    for name, make in BOOKS.items():
        (folder / name).write_bytes(make())


def check_tree(output):
    folder = output / "Audiobooks"
    return all((folder / name).is_file() and (folder / name).read_bytes() == make()
               for name, make in BOOKS.items()) and \
        sorted(path.name for path in folder.iterdir()) == sorted(BOOKS)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the synthetic M4B chapter fixtures")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.check:
        if not check_tree(arguments.output):
            print(f"out of date: {arguments.output}", file=sys.stderr)
            return 1
        return 0
    write_tree(arguments.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
