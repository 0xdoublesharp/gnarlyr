"""Realistic music volumes for the emulator, built once as sparse FAT32 images on Scratch.

A plan (artists, albums, tracks, tags, art) follows from the track count and the seed alone.
The image holds every file's metadata, embedded pictures, folder art, CUE sheets and tails;
audio frames are produced while the emulator reads them, so files keep realistic sizes.
"""
import bisect
import hashlib
import io
import json
import mmap
import os
import random
import struct
from pathlib import Path

from .fat_volume import Fat32Volume
from .flac_image import BLOCK_SAMPLES, FlacImage, SAMPLE_RATE, locate, streaminfo

GENERATOR_VERSION = 3
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho/data/scan-bench")
SECTOR = 512
SECTORS_PER_CLUSTER = 64
CARD_GIGABYTES = (32, 64, 128, 256, 512, 1024)
CARD_USABLE = 0.998
CARD_HEADROOM = 1.12
TRACKS_PER_ALBUM = (8, 14)
DISC_SPLIT_SHARE = 0.08
CUE_SHARE = 0.07
BOOK_SHARE = 0.03
COMPILATION_SHARE = 0.05
EMBEDDED_SHARE = 0.50
FOLDER_ONLY_SHARE = 0.35
EXTRA_FOLDER_SHARE = 0.40
PROGRESSIVE_SHARE = 0.25
PNG_SHARE = 0.10
NON_ASCII_SHARE = 0.18
ART_POOL = 480
ART_BYTES = (50 * 1024, 400 * 1024)
ART_SIDES = (500, 1500)
FORMAT_WEIGHTS = (("flac", 0.40), ("mp3", 0.40), ("m4a", 0.20))
FLAC_KBPS = (700, 1100)
MP3_KBPS = 320
M4A_KBPS = (192, 288)
BOOK_KBPS = 64
MP3_FRAME_HEADER = bytes([0xFF, 0xFB, 0xE0, 0x44])
MP3_FRAME_BYTES = 1044
MP3_SAMPLES = 1152
XING_OFFSET = 36
ID3V1_BYTES = 128
FLAC_PADDING = 8192
ID3_PADDING = 2048
MP4_TIMESCALE = 44100
AAC_FRAME_SAMPLES = 1024
CHAPTER_UNITS = 10000000
OPENING_TITLES = ("Aardvark Song", "Abacus Song", "Abalone Song")

WORDS = ("Love", "Night", "Blue", "Fire", "Dream", "Heart", "Road", "Light", "Rain", "Gold",
         "River", "Storm", "Ocean", "Shadow", "City", "Dance", "Echo", "Wild", "Stone", "Glass",
         "Silver", "Winter", "Summer", "Morning", "Electric", "Paper", "Static", "Velvet",
         "Neon", "Hollow", "Satellite", "Garden", "Mirror", "Desert", "Signal", "Harbor",
         "Lantern", "Thunder", "Feather", "Machine", "Orbit", "Copper", "Island", "Window",
         "Distance", "Fever", "Ghost", "Highway", "Kingdom", "Letters", "Midnight", "North")
FOREIGN = ("Été", "Café", "Ångström", "Mötley", "Naïve", "Smörgås", "Señorita", "Über",
           "Ça Va", "Déjà Vu", "Þögn", "Søndag", "Zürich", "Ñandú", "Œuvre", "Łódź",
           "Ночь", "Звезда", "Город", "Ветер", "夜明け", "東京", "さくら", "星空", "봄날",
           "Αθήνα", "Θάλασσα", "Kärlek", "Fête", "Crème")
FIRST_NAMES = ("Ada", "Ben", "Clara", "Dmitri", "Elena", "Felix", "Greta", "Hugo", "Iris",
               "Jonas", "Kai", "Lena", "Marco", "Nina", "Oskar", "Paula", "Quinn", "Rosa",
               "Sven", "Tara", "Ugo", "Vera", "Wim", "Yara", "Zoë", "Björn", "Inés", "Søren")
LAST_NAMES = ("Archer", "Baker", "Cole", "Dalton", "Ellis", "Frost", "Grant", "Hale", "Ivers",
              "Jensen", "Keller", "Lund", "Moreau", "Novak", "Ortega", "Price", "Quist",
              "Reyes", "Stone", "Thorne", "Varga", "Weiss", "Young", "Zeller", "Müller",
              "Šimek", "Ødegaard", "Côté")
BAND_SHAPES = ("The {0}s", "{0} {1}", "{0} & The {1}s", "{0}{1}", "Los {0}s", "{0} Club")
GENRES = ("Rock", "Pop", "Jazz", "Electronic", "Metal", "Classical", "Hip-Hop", "Folk", "Ambient",
          "Blues", "Soundtrack", "Indie", "Punk", "Soul", "Reggae", "Country", "Techno", "R&B")
FOLDER_ART = (("cover.jpg", 0.36), ("folder.jpg", 0.30), ("Cover.jpg", 0.08), ("front.jpg", 0.06),
              ("Folder.JPG", 0.05), ("AlbumArt.jpg", 0.05), ("cover.png", 0.06),
              ("folder.png", 0.04))
EXTRA_FILES = (("back.jpg", 0.12), ("rip.log", 0.10), ("info.txt", 0.05), ("playlist.m3u", 0.03))


def weighted(generator, choices):
    point = generator.random() * sum(weight for _, weight in choices)
    for value, weight in choices:
        point -= weight
        if point <= 0:
            return value
    return choices[-1][0]


def safe_name(text):
    cleaned = "".join("_" if character in '\\/:*?"<>|' else character for character in text)
    return cleaned.strip(" .") or "_"


class Namer:
    def __init__(self, generator):
        self.generator = generator
        self.used = set()

    def word(self):
        pool = FOREIGN if self.generator.random() < NON_ASCII_SHARE else WORDS
        return self.generator.choice(pool)

    def phrase(self, low, high):
        return " ".join(self.word() for _ in range(self.generator.randint(low, high)))

    def unique(self, make):
        for _ in range(50):
            name = make()
            if name.casefold() not in self.used:
                self.used.add(name.casefold())
                return name
        name = make() + " " + str(len(self.used))
        self.used.add(name.casefold())
        return name

    def artist(self):
        generator = self.generator
        if generator.random() < 0.4:
            return self.unique(lambda: generator.choice(FIRST_NAMES) + " " + generator.choice(LAST_NAMES))
        return self.unique(lambda: generator.choice(BAND_SHAPES).format(self.word(), self.word()))


def plan_track_count(generator, kind):
    if kind == "book":
        return generator.randint(10, 30)
    return generator.randint(*TRACKS_PER_ALBUM)


def album_kind(generator):
    point = generator.random()
    if point < CUE_SHARE:
        return "cue"
    if point < CUE_SHARE + BOOK_SHARE:
        return "book"
    return "tracks"


def art_choice(generator, kind):
    point = generator.random()
    if kind == "book":
        return {"embedded": point < 0.8, "folder": None}
    if point < EMBEDDED_SHARE:
        extra = generator.random() < EXTRA_FOLDER_SHARE
        return {"embedded": kind != "cue" or generator.random() < 0.5,
                "folder": weighted(generator, FOLDER_ART) if extra or kind == "cue" else None}
    if point < EMBEDDED_SHARE + FOLDER_ONLY_SHARE:
        return {"embedded": False, "folder": weighted(generator, FOLDER_ART)}
    return {"embedded": False, "folder": None}


def plan_album(generator, namer, artist, number):
    kind = album_kind(generator)
    compilation = kind == "tracks" and generator.random() < COMPILATION_SHARE
    count = plan_track_count(generator, kind)
    discs = 2 if kind == "tracks" and generator.random() < DISC_SPLIT_SHARE else 1
    title = namer.unique(lambda: namer.phrase(1, 3))
    album = {
        "number": number,
        "kind": kind,
        "artist": "Various Artists" if compilation else artist,
        "title": title,
        "year": generator.randint(1962, 2025),
        "genre": "Audiobook" if kind == "book" else generator.choice(GENRES),
        "format": "m4b" if kind == "book" else ("flac" if kind == "cue" else weighted(generator, FORMAT_WEIGHTS)),
        "discs": discs,
        "disc_folders": discs > 1 and generator.random() < 0.5,
        "art": art_choice(generator, kind),
        "art_index": number % ART_POOL,
        "extras": [name for name, share in EXTRA_FILES if generator.random() < share],
        "id3v1": generator.random() < 0.7,
        "id3v24": generator.random() < 0.5,
        "xing": generator.random() < 0.8,
        "moov_last": generator.random() < 0.3,
        "picture_first": generator.random() < 0.3,
        "tracks": [],
    }
    per_disc = (count + discs - 1) // discs
    for index in range(count):
        album["tracks"].append({
            "disc": index // per_disc + 1,
            "number": index % per_disc + 1,
            "title": namer.phrase(1, 4),
            "artist": namer.artist() if compilation and generator.random() < 0.8 else artist,
            "seconds": generator.randint(600, 4800) if kind == "book" else generator.randint(140, 420),
        })
    album["disc_total"] = per_disc
    return album


def plan_library(tracks, seed):
    generator = random.Random(seed)
    namer = Namer(generator)
    albums = []
    total = 0
    while total < tracks:
        artist = namer.artist()
        for _ in range(min(6, max(1, int(generator.expovariate(1 / 2.2)) + 1))):
            if total >= tracks:
                break
            album = plan_album(generator, namer, artist, len(albums))
            albums.append(album)
            total += len(album["tracks"])
    for album in albums:
        place_album(album)
    lead_playable_albums(albums)
    return albums


def lead_playable_albums(albums):
    """Only FLAC plays in the emulator, so the first songs by title come from FLAC albums with
    art: Now Playing can then change album on a skip in All Songs."""
    chosen = [album for album in albums if album["kind"] == "tracks" and album["format"] == "flac"
              and (album["art"]["embedded"] or album["art"]["folder"])][:len(OPENING_TITLES)]
    for album, title in zip(chosen, OPENING_TITLES):
        album["tracks"][0]["title"] = title


def place_album(album):
    if album["kind"] == "book":
        album["folder"] = "Audiobooks/" + safe_name(album["artist"])
        return
    year = " (%d)" % album["year"] if album["number"] % 3 else ""
    album["folder"] = safe_name(album["artist"]) + "/" + safe_name(album["title"] + year)


def track_folder(album, track):
    if album["disc_folders"]:
        return album["folder"] + "/CD%d" % track["disc"]
    return album["folder"]


def track_name(album, track, extension):
    prefix = "%d-%02d" % (track["disc"], track["number"]) if album["discs"] > 1 and not album["disc_folders"] else "%02d" % track["number"]
    separator = " - " if album["number"] % 2 else " "
    return safe_name(prefix + separator + track["title"]) + "." + extension


def art_base(seed, index):
    from PIL import Image, ImageDraw
    generator = random.Random("%s-art-%d" % (seed, index))
    side = generator.randint(*ART_SIDES)
    base = Image.new("RGB", (side, side), tuple(generator.randrange(256) for _ in range(3)))
    draw = ImageDraw.Draw(base)
    for _ in range(generator.randint(6, 30)):
        xs = sorted(generator.randrange(side) for _ in range(2))
        ys = sorted(generator.randrange(side) for _ in range(2))
        fill = tuple(generator.randrange(256) for _ in range(3))
        if generator.random() < 0.5:
            draw.ellipse((xs[0], ys[0], xs[1], ys[1]), fill=fill)
        else:
            draw.rectangle((xs[0], ys[0], xs[1], ys[1]), fill=fill)
    return base, generator


def encode_art(base, generator, kind):
    from PIL import Image
    side = base.size[0]
    if kind == "png":
        small = base.resize((side * 2 // 3, side * 2 // 3)).quantize(64).convert("RGB")
        buffer = io.BytesIO()
        small.save(buffer, "PNG", optimize=False)
        return buffer.getvalue()
    target = generator.randint(*ART_BYTES)
    progressive = generator.random() < PROGRESSIVE_SHARE
    quality = generator.randint(82, 95)
    noise = Image.frombytes("RGB", base.size, generator.randbytes(side * side * 3))
    alpha = 0.08
    data = b""
    for _ in range(5):
        image = Image.blend(base, noise, alpha)
        buffer = io.BytesIO()
        image.save(buffer, "JPEG", quality=quality, progressive=progressive, optimize=False)
        data = buffer.getvalue()
        if ART_BYTES[0] <= len(data) <= ART_BYTES[1] and abs(len(data) - target) < target // 3:
            break
        alpha = max(0.002, min(0.6, alpha * (target / max(1, len(data))) ** 1.3))
    return data


class ArtStore:
    def __init__(self, seed):
        self.seed = seed
        self.encoded = {}

    def bytes_for(self, index, kind):
        key = (index % ART_POOL, kind)
        if key not in self.encoded:
            base, generator = art_base(self.seed, key[0])
            self.encoded[key] = encode_art(base, generator, kind)
        return self.encoded[key]


class FileContent:
    """A file as a size, stored byte runs and frames produced while it is read."""

    def __init__(self, size):
        self.size = size
        self.pieces = []
        self.virtual = []

    def put(self, offset, data):
        self.pieces.append((offset, bytes(data)))

    def frames(self, start, end, kind, parameter=0):
        if end > start:
            self.virtual.append((start, end, kind, parameter))


def vorbis_comments(fields):
    vendor = b"reference libFLAC 1.4.3 20230623"
    body = struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", len(fields))
    for key, value in fields:
        entry = ("%s=%s" % (key, value)).encode("utf-8")
        body += struct.pack("<I", len(entry)) + entry
    return body


def flac_block(kind, body, last=False):
    return bytes([kind | (0x80 if last else 0)]) + len(body).to_bytes(3, "big") + body


def picture_block(picture, mime):
    mime_bytes = mime.encode("ascii")
    description = b""
    return (struct.pack(">I", 3) + struct.pack(">I", len(mime_bytes)) + mime_bytes
            + struct.pack(">I", len(description)) + description
            + struct.pack(">IIII", 1000, 1000, 24, 0) + struct.pack(">I", len(picture)) + picture)


def mime_of(picture):
    return "image/png" if picture[:4] == b"\x89PNG" else "image/jpeg"


def gain_fields(generator):
    return [("REPLAYGAIN_TRACK_GAIN", "%+.2f dB" % generator.uniform(-11, 2)),
            ("REPLAYGAIN_TRACK_PEAK", "%.6f" % generator.uniform(0.6, 1.0)),
            ("REPLAYGAIN_ALBUM_GAIN", "%+.2f dB" % generator.uniform(-10, 1)),
            ("REPLAYGAIN_ALBUM_PEAK", "%.6f" % generator.uniform(0.8, 1.0))]


def uuid_text(generator):
    raw = generator.randbytes(16).hex()
    return "%s-%s-%s-%s-%s" % (raw[:8], raw[8:12], raw[12:16], raw[16:20], raw[20:])


def flac_fields(album, track, generator, cue=False):
    fields = [("ALBUM", album["title"]), ("ALBUMARTIST", album["artist"]),
              ("DATE", str(album["year"])), ("GENRE", album["genre"])]
    if not cue:
        fields += [("TITLE", track["title"]), ("ARTIST", track["artist"]),
                   ("TRACKNUMBER", str(track["number"])), ("TRACKTOTAL", str(album["disc_total"])),
                   ("DISCNUMBER", str(track["disc"])), ("DISCTOTAL", str(album["discs"]))]
    else:
        fields += [("ARTIST", album["artist"])]
    fields += gain_fields(generator)
    fields += [("MUSICBRAINZ_ALBUMID", uuid_text(generator)),
               ("MUSICBRAINZ_TRACKID", uuid_text(generator)),
               ("ENCODER", "reference libFLAC 1.4.3")]
    return fields


def flac_file(album, track, generator, picture, seconds, cue=False):
    samples = seconds * SAMPLE_RATE
    info = flac_block(0, streaminfo(samples))
    points = max(1, seconds // 10)
    seek = flac_block(3, b"".join(struct.pack(">QQH", index * 10 * SAMPLE_RATE, 0, BLOCK_SAMPLES)
                                  for index in range(points)))
    comments = flac_block(4, vorbis_comments(flac_fields(album, track, generator, cue)))
    blocks = [seek, comments]
    if picture is not None:
        art = flac_block(6, picture_block(picture, mime_of(picture)))
        blocks = [seek, art, comments] if album["picture_first"] else [seek, comments, art]
    head = b"fLaC" + info + b"".join(blocks) + flac_block(1, bytes(FLAC_PADDING), last=True)
    kbps = generator.randint(*FLAC_KBPS)
    size = max(len(head) + 4096, seconds * kbps * 125)
    content = FileContent(size)
    content.put(0, head)
    content.frames(len(head), size, "flac")
    return content


def syncsafe(value):
    return bytes([(value >> 21) & 0x7F, (value >> 14) & 0x7F, (value >> 7) & 0x7F, value & 0x7F])


def id3_frame(frame_id, data, version):
    size = syncsafe(len(data)) if version == 4 else len(data).to_bytes(4, "big")
    return frame_id.encode("ascii") + size + b"\x00\x00" + data


def id3_text(frame_id, value, version):
    if version == 4:
        return id3_frame(frame_id, b"\x03" + value.encode("utf-8"), version)
    return id3_frame(frame_id, b"\x01\xff\xfe" + value.encode("utf-16-le"), version)


def id3_tag(album, track, generator, picture):
    version = 4 if album["id3v24"] else 3
    frames = [id3_text("TIT2", track["title"], version), id3_text("TPE1", track["artist"], version),
              id3_text("TPE2", album["artist"], version), id3_text("TALB", album["title"], version),
              id3_text("TCON", album["genre"], version),
              id3_text("TDRC" if version == 4 else "TYER", str(album["year"]), version),
              id3_text("TRCK", "%d/%d" % (track["number"], album["disc_total"]), version),
              id3_text("TPOS", "%d/%d" % (track["disc"], album["discs"]), version),
              id3_text("TSSE", "LAME 3.100", version)]
    for key, value in gain_fields(generator)[:2]:
        frames.append(id3_frame("TXXX", b"\x00" + key.encode("ascii") + b"\x00" + value.encode("ascii"), version))
    if picture is not None:
        frames.append(id3_frame("APIC", b"\x00" + mime_of(picture).encode("ascii") + b"\x00\x03\x00" + picture,
                                version))
    body = b"".join(frames) + bytes(ID3_PADDING)
    return b"ID3" + bytes([version, 0, 0]) + syncsafe(len(body)) + body


def id3v1_tag(album, track):
    def field(text, length):
        return text.encode("latin-1", "replace")[:length].ljust(length, b"\x00")

    return (b"TAG" + field(track["title"], 30) + field(track["artist"], 30) + field(album["title"], 30)
            + field(str(album["year"]), 4) + bytes(28) + b"\x00" + bytes([track["number"] & 0xFF]) + b"\xff")


def xing_frame(frames, size):
    frame = bytearray(MP3_FRAME_BYTES)
    frame[0:4] = MP3_FRAME_HEADER
    frame[XING_OFFSET:XING_OFFSET + 8] = b"Xing" + struct.pack(">I", 0x0F)
    struct.pack_into(">II", frame, XING_OFFSET + 8, frames, size)
    frame[XING_OFFSET + 16:XING_OFFSET + 116] = bytes(min(255, index * 256 // 100) for index in range(100))
    struct.pack_into(">I", frame, XING_OFFSET + 116, 78)
    return bytes(frame)


def mp3_file(album, track, generator, picture, seconds):
    head = id3_tag(album, track, generator, picture)
    frames = seconds * SAMPLE_RATE // MP3_SAMPLES
    size = len(head) + frames * MP3_FRAME_BYTES + (ID3V1_BYTES if album["id3v1"] else 0)
    content = FileContent(size)
    if album["xing"]:
        head += xing_frame(frames, size)
    content.put(0, head)
    end = size - (ID3V1_BYTES if album["id3v1"] else 0)
    content.frames(len(head), end, "mp3")
    if album["id3v1"]:
        content.put(end, id3v1_tag(album, track))
    return content


def box(kind, *parts):
    body = b"".join(parts)
    return struct.pack(">I", 8 + len(body)) + kind.encode("latin-1") + body


def full_box(kind, *parts, version=0, flags=0):
    return box(kind, struct.pack(">I", version << 24 | flags), *parts)


def text_item(kind, value):
    return box(kind, full_box("data", struct.pack(">I", 0), value.encode("utf-8"), flags=1))


def pair_item(kind, number, total):
    return box(kind, full_box("data", struct.pack(">I", 0), struct.pack(">HHHH", 0, number, total, 0)))


def cover_item(picture):
    flag = 14 if picture[:4] == b"\x89PNG" else 13
    return box("covr", full_box("data", struct.pack(">I", 0), picture, flags=flag))


def free_form(name, value):
    return box("----", full_box("mean", b"com.apple.iTunes"), full_box("name", name.encode("ascii")),
               full_box("data", struct.pack(">I", 0), value.encode("ascii"), flags=1))


def mp4_tags(album, track, picture, chapters):
    items = [text_item("\xa9nam", track["title"] if not chapters else album["title"]),
             text_item("\xa9ART", track["artist"]), text_item("aART", album["artist"]),
             text_item("\xa9alb", album["title"]), text_item("\xa9gen", album["genre"]),
             text_item("\xa9day", "%d-03-01T08:00:00Z" % album["year"]),
             pair_item("trkn", track["number"], album["disc_total"]),
             pair_item("disk", track["disc"], album["discs"]), text_item("\xa9too", "iTunes 12.12"),
             free_form("iTunNORM", " 00000A2C 00000B1F 00004E21 00005F3D 0001ADB0")]
    if picture is not None:
        items.append(cover_item(picture))
    handler = full_box("hdlr", bytes(4), b"mdir", b"appl", bytes(9))
    return full_box("meta", handler, box("ilst", *items))


def chapter_list(entries):
    rows = b"".join(struct.pack(">QB", start * CHAPTER_UNITS, len(name.encode("utf-8"))) + name.encode("utf-8")
                    for start, name in entries)
    return full_box("chpl", bytes(4), struct.pack(">B", len(entries)), rows, version=1)


def sound_track(duration, frames):
    tkhd = full_box("tkhd", struct.pack(">IIIII", 0, 0, 1, 0, duration), bytes(60), flags=3)
    mdhd = full_box("mdhd", struct.pack(">IIIIHH", 0, 0, MP4_TIMESCALE, duration, 0x55C4, 0))
    hdlr = full_box("hdlr", struct.pack(">I4s", 0, b"soun"), bytes(12), b"SoundHandler\x00")
    esds = full_box("esds", bytes.fromhex("03190000000411401500000000000000000000000502121006800000"))
    entry = box("mp4a", bytes(6), struct.pack(">H", 1), bytes(8), struct.pack(">HHHHI", 2, 16, 0, 0,
                                                                                MP4_TIMESCALE << 16), esds)
    stsd = full_box("stsd", struct.pack(">I", 1), entry)
    stts = full_box("stts", struct.pack(">III", 1, frames, AAC_FRAME_SAMPLES))
    stsc = full_box("stsc", struct.pack(">IIII", 1, 1, 22, 1))
    stsz = full_box("stsz", struct.pack(">II", 0, frames), bytes(4 * frames))
    chunks = (frames + 21) // 22
    stco = full_box("stco", struct.pack(">I", chunks), bytes(4 * chunks))
    stbl = box("stbl", stsd, stts, stsc, stsz, stco)
    minf = box("minf", full_box("smhd", bytes(4)), box("dinf", full_box("dref", struct.pack(">I", 1),
                                                                         full_box("url ", flags=1))), stbl)
    return box("trak", tkhd, box("mdia", mdhd, hdlr, minf))


def mp4_file(album, track, generator, picture, seconds, chapters=None):
    duration = seconds * MP4_TIMESCALE
    frames = duration // AAC_FRAME_SAMPLES
    mvhd = full_box("mvhd", struct.pack(">IIII", 0, 0, MP4_TIMESCALE, duration), bytes(80))
    udta = [mp4_tags(album, track, picture, chapters)]
    if chapters:
        udta.insert(0, chapter_list(chapters))
    moov = box("moov", mvhd, sound_track(duration, frames), box("udta", *udta))
    ftyp = box("ftyp", b"M4B " if chapters else b"M4A ", struct.pack(">I", 0), b"M4A mp42isom\x00\x00\x00\x00")
    kbps = BOOK_KBPS if chapters else generator.randint(*M4A_KBPS)
    payload = seconds * kbps * 125
    mdat_header = struct.pack(">I", payload + 8) + b"mdat"
    size = len(ftyp) + len(moov) + 8 + payload
    content = FileContent(size)
    if album["moov_last"] and not chapters:
        content.put(0, ftyp + mdat_header)
        content.put(len(ftyp) + 8 + payload, moov)
    else:
        content.put(0, ftyp + moov + mdat_header)
    return content


def cue_sheet(album, audio_name):
    lines = ['REM GENRE "%s"' % album["genre"], "REM DATE %d" % album["year"],
             'REM COMMENT "ExactAudioCopy v1.6"', 'PERFORMER "%s"' % album["artist"],
             'TITLE "%s"' % album["title"], 'FILE "%s" WAVE' % audio_name]
    start = 0
    for track in album["tracks"]:
        lines += ["  TRACK %02d AUDIO" % track["number"], '    TITLE "%s"' % track["title"],
                  '    PERFORMER "%s"' % track["artist"],
                  "    INDEX 01 %02d:%02d:00" % (start // 60, start % 60)]
        start += track["seconds"]
    return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def book_chapters(album):
    entries = []
    start = 0
    for index, track in enumerate(album["tracks"]):
        entries.append((start, "Chapter %d: %s" % (index + 1, track["title"])))
        start += track["seconds"]
    return entries, start


class Planned:
    """A file whose content is made when it is written, so the plan holds sizes only."""

    def __init__(self, folder, name, make):
        self.folder = folder
        self.name = name
        self.make = make
        self.size = make().size

    def content(self):
        return self.make()


def stored(data):
    content = FileContent(len(data))
    content.put(0, data)
    return content


def file_generator(seed, folder, name):
    return random.Random("%s|%s/%s" % (seed, folder, name))


def album_files(album, art, seed):
    picture_kind = "png" if album["art_index"] % 10 == 9 else "jpg"

    def picture():
        return art.bytes_for(album["art_index"], picture_kind) if album["art"]["embedded"] else None

    def planned(folder, name, make):
        return Planned(folder, name, lambda: make(file_generator(seed, folder, name)))

    files = []
    if album["kind"] == "book":
        chapters, seconds = book_chapters(album)
        first = dict(album["tracks"][0], title=album["title"], artist=album["artist"], number=1, disc=1)
        files.append(planned(album["folder"], safe_name(album["title"]) + ".m4b",
                             lambda generator: mp4_file(album, first, generator, picture(), seconds, chapters)))
    elif album["kind"] == "cue":
        seconds = sum(track["seconds"] for track in album["tracks"])
        base = safe_name(album["artist"] + " - " + album["title"])
        files.append(planned(album["folder"], base + ".flac",
                             lambda generator: flac_file(album, album["tracks"][0], generator, picture(),
                                                         seconds, cue=True)))
        sheet = cue_sheet(album, base + ".flac")
        files.append(planned(album["folder"], base + ".cue", lambda generator: stored(sheet)))
    else:
        makers = {"flac": flac_file, "mp3": mp3_file, "m4a": mp4_file}
        for track in album["tracks"]:
            extension = album["format"]
            files.append(planned(track_folder(album, track), track_name(album, track, extension),
                                 lambda generator, track=track, make=makers[extension]:
                                 make(album, track, generator, picture(), track["seconds"])))
    folder_art = album["art"]["folder"]
    if folder_art:
        kind = "png" if folder_art.lower().endswith(".png") else "jpg"
        files.append(planned(album["folder"], folder_art,
                             lambda generator: stored(art.bytes_for(album["art_index"], kind))))
    for extra in album["extras"]:
        if extra == "back.jpg":
            files.append(planned(album["folder"], extra,
                                 lambda generator: stored(art.bytes_for(album["art_index"] + 7, "jpg"))))
        else:
            text = ("%s\r\n" % album["title"]).encode("utf-8") * 40
            files.append(planned(album["folder"], extra, lambda generator, text=text: stored(text)))
    return files


def macos_files():
    def padded(head, size):
        def make():
            content = FileContent(size)
            content.put(0, head)
            return content

        return make

    files = [Planned("", ".DS_Store", padded(b"\x00\x00\x00\x01Bud1", 6148)),
             Planned("", "._.DS_Store", padded(b"\x00\x05\x16\x07", 4096))]
    for index in range(120):
        files.append(Planned(".fseventsd", "%016x" % (0x1B7A000 + index), padded(bytes([1]) * 64, 64)))
    for index in range(300):
        files.append(Planned(".Spotlight-V100/Store-V2", "%d.directoryStoreFile.shadow" % index,
                             padded(b"\x01" * 16, 4096)))
    return files


def card_gigabytes(total_bytes):
    for gigabytes in CARD_GIGABYTES:
        if gigabytes * 1e9 * CARD_USABLE >= total_bytes * CARD_HEADROOM:
            return gigabytes
    raise ValueError("the library needs more than %d GB" % CARD_GIGABYTES[-1])


class ImageWriter:
    def __init__(self, path, size):
        self.handle = open(path, "wb")
        self.handle.truncate(size)
        self.virtual = []
        self.stored = 0

    def write(self, lba, data):
        os.pwrite(self.handle.fileno(), data, lba * SECTOR)
        self.stored += len(data)

    def add_source(self, lba, size, planned):
        base = lba * SECTOR
        content = planned.content()
        for offset, data in content.pieces:
            os.pwrite(self.handle.fileno(), data, base + offset)
            self.stored += len(data)
        for start, end, kind, parameter in content.virtual:
            self.virtual.append((base + start, base + end, kind, parameter))

    def close(self):
        self.handle.close()


def directories_of(files):
    folders = set()
    for planned in files:
        parts = [part for part in planned.folder.split("/") if part]
        for depth in range(1, len(parts) + 1):
            folders.add("/".join(parts[:depth]))
    return sorted(folders, key=lambda path: (path.count("/"), path.casefold()))


def lay_out(files, megabytes):
    volume = Fat32Volume(megabytes, SECTORS_PER_CLUSTER, fsinfo=True)
    for folder in directories_of(files):
        parent, _, name = folder.rpartition("/")
        volume.add_directory(name, parent)
    for planned in sorted(files, key=lambda item: (item.folder.casefold(), item.name.casefold())):
        volume.add_file(planned.name, None, None, planned.folder, planned)
    return volume


def parameters_key(tracks, seed):
    from PIL import __version__ as pillow
    text = json.dumps({"tracks": tracks, "seed": seed, "version": GENERATOR_VERSION, "pillow": pillow},
                      sort_keys=True)
    return "t%d-s%s-%s" % (tracks, seed, hashlib.sha256(text.encode()).hexdigest()[:10])


def summarize(albums, files, art):
    sizes = sorted(len(data) for data in art.encoded.values())
    formats = {}
    for album in albums:
        formats[album["format"]] = formats.get(album["format"], 0) + len(album["tracks"])
    return {
        "albums": len(albums),
        "planned_tracks": sum(len(album["tracks"]) for album in albums),
        "files": len(files),
        "audio_files": sum(1 for planned in files if planned.name.rsplit(".", 1)[-1].lower()
                           in ("flac", "mp3", "m4a", "m4b")),
        "tracks_by_format": formats,
        "cue_albums": sum(1 for album in albums if album["kind"] == "cue"),
        "books": sum(1 for album in albums if album["kind"] == "book"),
        "albums_with_embedded_art": sum(1 for album in albums if album["art"]["embedded"]),
        "albums_with_folder_art_only": sum(1 for album in albums
                                           if album["art"]["folder"] and not album["art"]["embedded"]),
        "albums_without_art": sum(1 for album in albums
                                  if not album["art"]["folder"] and not album["art"]["embedded"]),
        "art_images": len(sizes),
        "art_bytes_median": sizes[len(sizes) // 2] if sizes else 0,
        "art_bytes_range": [sizes[0], sizes[-1]] if sizes else [0, 0],
        "music_bytes": sum(planned.size for planned in files),
    }


def build(directory, tracks, seed, log=print):
    directory.mkdir(parents=True, exist_ok=True)
    albums = plan_library(tracks, seed)
    log("planned %d albums, %d tracks" % (len(albums), sum(len(album["tracks"]) for album in albums)))
    art = ArtStore(seed)
    files = macos_files()
    for album in albums:
        files.extend(album_files(album, art, seed))
    log("made %d files and %d art images" % (len(files), len(art.encoded)))
    summary = summarize(albums, files, art)
    gigabytes = card_gigabytes(summary["music_bytes"])
    megabytes = int(gigabytes * 1e9 * CARD_USABLE) // (1024 * 1024)
    volume = lay_out(files, megabytes)
    temporary = directory / "volume.img.partial"
    writer = ImageWriter(temporary, volume.sector_count * SECTOR)
    volume.write_to(writer, 0)
    writer.close()
    temporary.rename(directory / "volume.img")
    manifest = {
        "tracks": tracks, "seed": seed, "version": GENERATOR_VERSION,
        "file_system": "FAT32", "card_gigabytes": gigabytes, "megabytes": megabytes,
        "sectors_per_cluster": SECTORS_PER_CLUSTER, "sector_count": volume.sector_count,
        "fat_sectors": volume.fat_sectors, "data_start": volume.data_start,
        "used_clusters": volume.used_clusters, "stored_bytes": writer.stored,
        "virtual": sorted(writer.virtual), "summary": summary,
        "albums": [{"folder": album["folder"], "title": album["title"], "artist": album["artist"],
                    "kind": album["kind"], "format": album["format"], "tracks": len(album["tracks"]),
                    "embedded": album["art"]["embedded"], "folder_art": album["art"]["folder"]}
                   for album in albums],
    }
    (directory / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False))
    return manifest


def cached(tracks, seed, root=SCRATCH, log=print):
    directory = Path(root) / parameters_key(tracks, seed)
    manifest_path = directory / "manifest.json"
    if (directory / "volume.img").is_file() and manifest_path.is_file():
        log("reusing %s" % directory)
        return LibraryVolume(directory)
    build(directory, tracks, seed, log)
    return LibraryVolume(directory)


class VolumeReader:
    def __init__(self, path, virtual, size):
        self.handle = open(path, "rb")
        self.map = mmap.mmap(self.handle.fileno(), 0, access=mmap.ACCESS_READ)
        self.size = size
        self.virtual = virtual
        self.starts = [item[0] for item in virtual]
        self.flac = FlacImage(1)
        self.mp3 = MP3_FRAME_HEADER + bytes(MP3_FRAME_BYTES - len(MP3_FRAME_HEADER))

    def read(self, offset, length):
        data = self.map[offset:offset + length]
        position = bisect.bisect_right(self.starts, offset + length - 1) - 1
        out = None
        while position >= 0 and self.virtual[position][1] > offset:
            start, end, kind, parameter = self.virtual[position]
            out = out or bytearray(data)
            first = max(offset, start)
            last = min(offset + length, end)
            out[first - offset:last - offset] = self.produce(kind, parameter, first - start, last - first)
            position -= 1
        return data if out is None else bytes(out)

    def produce(self, kind, parameter, offset, length):
        out = bytearray()
        while len(out) < length:
            if kind == "flac":
                number, within = locate(offset + len(out))
                chunk = self.flac.frame(number)[within:]
            else:
                within = (offset + len(out) + parameter) % MP3_FRAME_BYTES
                chunk = self.mp3[within:]
            out += chunk[:length - len(out)]
        return bytes(out)


class LibraryVolume:
    """A built library, given to `Session(volume=...)` in place of a `Fat32Volume`."""

    def __init__(self, directory):
        self.directory = Path(directory)
        self.manifest = json.loads((self.directory / "manifest.json").read_text())
        self.sector_count = self.manifest["sector_count"]
        self.fat_sectors = self.manifest["fat_sectors"]
        self.data_start = self.manifest["data_start"]
        self.sectors_per_cluster = self.manifest["sectors_per_cluster"]
        self.reader = None

    def write_to(self, disk, start):
        if self.reader is None:
            self.reader = VolumeReader(self.directory / "volume.img",
                                       [tuple(item) for item in self.manifest["virtual"]],
                                       self.sector_count * SECTOR)
        disk.add_source(start, self.sector_count * SECTOR, self.reader)

    def cluster_lba(self, cluster):
        return self.data_start + (cluster - 2) * self.sectors_per_cluster


def remove(directory):
    directory = Path(directory)
    for name in ("volume.img", "volume.img.partial", "manifest.json"):
        path = directory / name
        if path.exists():
            path.unlink()
    if directory.exists() and not any(directory.iterdir()):
        directory.rmdir()
