import argparse
import base64
import io
import random
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from mutagen import id3
from mutagen.flac import FLAC, Picture
from mutagen.mp4 import MP4, MP4Cover
from mutagen.oggopus import OggOpus
from mutagen.oggvorbis import OggVorbis
from PIL import Image, ImageDraw, ImageOps

import check_cover_decode
import cover_export

FULL = 112
HALF = 56
RECORD_BYTES = (FULL * FULL + HALF * HALF) * 2
HEADER = struct.Struct("<4sHHHHI")
ENTRY = struct.Struct("<II")
ARTIST = "Tester"
FRONT = 3
OTHER = 0
BACK = 4
SHOWN_LIMIT = 16.0
PENDING = {"id3v23_unsync", "id3v24_frame_unsync", "ogg_vorbis_picture", "ogg_vorbis_large_picture",
           "opus_front_second"}
ORIENTATION_TAG = 0x0112
ID3_SOURCE = "id3v24.mp3"


@dataclass
class Album:
    name: str
    candidates: dict = field(default_factory=dict)
    expected: str = None
    note: str = ""


def palette_for(seed):
    generator = random.Random(seed)
    return [tuple(generator.randrange(256) for _ in range(3)) for _ in range(4)]


def artwork(width, height, seed, noise=0.0):
    colors = palette_for(seed)
    image = Image.linear_gradient("L").resize((width, height)).convert("RGB")
    image = Image.blend(Image.new("RGB", (width, height), colors[0]), image, 0.35)
    draw = ImageDraw.Draw(image)
    generator = random.Random(seed * 7 + 1)
    for index in range(10):
        left, top = generator.randrange(width), generator.randrange(height)
        size = max(2, min(width, height) * generator.randrange(10, 45) // 100)
        shape = draw.ellipse if index % 2 else draw.rectangle
        shape((left - size, top - size, left + size, top + size // 2), fill=colors[1 + index % 3])
    draw.rectangle((0, 0, width // 3, height // 4), fill=colors[3])
    if noise > 0:
        grain = Image.frombytes("RGB", (width, height),
                                random.Random(seed).randbytes(width * height * 3))
        image = Image.blend(image, grain, noise)
    return image


def jpeg(image, **options):
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", **options)
    return buffer.getvalue()


def png(image, **options):
    buffer = io.BytesIO()
    image.save(buffer, "PNG", **options)
    return buffer.getvalue()


def decoded(data):
    image = Image.open(io.BytesIO(data))
    image.load()
    return image


def with_orientation(image, orientation, **options):
    exif = Image.Exif()
    exif[ORIENTATION_TAG] = orientation
    return jpeg(image, exif=exif.tobytes(), **options)


def interlaced_png(image):
    width, height = image.size
    pixels = list(image.convert("RGB").getdata())
    samples = [[pixels[y * width + x] for x in range(width)] for y in range(height)]
    return check_cover_decode.png_bytes(samples, width, height, 8, 2, 3, interlace=True)


def sixteen_bit_png(image):
    width, height = image.size
    pixels = list(image.convert("RGB").getdata())
    samples = [[pixels[y * width + x] for x in range(width)] for y in range(height)]
    return check_cover_decode.png_bytes(samples, width, height, 16, 2, 3)


def cmyk_jpeg(image, progressive=False):
    return jpeg(image.convert("CMYK"), quality=92, progressive=progressive)


def unsupported_sof(data):
    patched = bytearray(data)
    marker = patched.find(b"\xff\xc0")
    patched[marker + 1] = 0xC3
    return bytes(patched)


def gif(image):
    buffer = io.BytesIO()
    image.save(buffer, "GIF")
    return buffer.getvalue()


def flac_track(path, source, album, pictures, number=1):
    shutil.copyfile(source / "tagged.flac", path)
    audio = FLAC(path)
    audio.delete()
    audio.clear_pictures()
    audio["album"], audio["albumartist"], audio["artist"] = album, ARTIST, ARTIST
    audio["title"], audio["tracknumber"] = f"{album} {number}", str(number)
    for kind, mime, data in pictures:
        block = Picture()
        block.type, block.mime, block.data = kind, mime, data
        audio.add_picture(block)
    audio.save()


def mp3_audio(source):
    data = (source / ID3_SOURCE).read_bytes()
    size = data[6] << 21 | data[7] << 14 | data[8] << 7 | data[9]
    return data[10 + size:]


def mp3_track(path, source, album, pictures, version=4):
    path.write_bytes(mp3_audio(source))
    tags = id3.ID3()
    for frame in (id3.TALB(text=album), id3.TPE2(text=ARTIST), id3.TPE1(text=ARTIST),
                  id3.TIT2(text=f"{album} 1"), id3.TRCK(text="1")):
        tags.add(frame)
    for index, (kind, mime, data) in enumerate(pictures):
        tags.add(id3.APIC(encoding=0, mime=mime, type=kind, desc=str(index), data=data))
    tags.save(path, v2_version=version)


def unsynchronised(data):
    out = bytearray()
    for index, byte in enumerate(data):
        out.append(byte)
        following = data[index + 1] if index + 1 < len(data) else None
        if byte == 0xFF and (following is None or following == 0 or following >= 0xE0):
            out.append(0)
    return bytes(out)


def syncsafe(value):
    return bytes([(value >> 21) & 0x7F, (value >> 14) & 0x7F, (value >> 7) & 0x7F, value & 0x7F])


def text_body(text):
    return b"\x01\xff\xfe" + text.encode("utf-16-le")


def picture_body(kind, mime, data):
    return b"\x00" + mime.encode("ascii") + b"\x00" + bytes([kind]) + b"cover\x00" + data


def v23_unsync_tag(album, picture):
    frames = b""
    for name, value in (("TALB", album), ("TPE2", ARTIST), ("TPE1", ARTIST),
                        ("TIT2", f"{album} 1"), ("TRCK", "1")):
        body = text_body(value)
        frames += name.encode() + struct.pack(">I", len(body)) + b"\x00\x00" + body
    body = picture_body(*picture)
    frames += b"APIC" + struct.pack(">I", len(body)) + b"\x00\x00" + body
    raw = unsynchronised(frames)
    return b"ID3\x03\x00\x80" + syncsafe(len(raw)) + raw


def v24_frame_unsync_tag(album, picture):
    frames = b""
    for name, value in (("TALB", album), ("TPE2", ARTIST), ("TPE1", ARTIST),
                        ("TIT2", f"{album} 1"), ("TRCK", "1")):
        body = b"\x03" + value.encode("utf-8")
        frames += name.encode() + syncsafe(len(body)) + b"\x00\x00" + body
    body = picture_body(*picture)
    raw = syncsafe(len(body)) + unsynchronised(body)
    frames += b"APIC" + syncsafe(len(raw)) + b"\x00\x03" + raw
    return b"ID3\x04\x00\x00" + syncsafe(len(frames)) + frames


def raw_mp3_track(path, source, tag):
    path.write_bytes(tag + mp3_audio(source))


def mp4_track(path, source, album, covers):
    shutil.copyfile(source / "aac.m4a", path)
    audio = MP4(path)
    audio.delete()
    audio["\xa9alb"], audio["aART"], audio["\xa9ART"] = [album], [ARTIST], [ARTIST]
    audio["\xa9nam"], audio["trkn"] = [f"{album} 1"], [(1, 0)]
    audio["covr"] = [MP4Cover(data, imageformat=kind) for kind, data in covers]
    audio.save()


def picture_text(kind, mime, data):
    block = Picture()
    block.type, block.mime, block.data = kind, mime, data
    return base64.b64encode(block.write()).decode("ascii")


def ogg_track(path, source, album, pictures, opus=False):
    shutil.copyfile(source / ("tagged.opus" if opus else "tagged.ogg"), path)
    audio = OggOpus(path) if opus else OggVorbis(path)
    audio.delete()
    audio["album"], audio["albumartist"], audio["artist"] = [album], [ARTIST], [ARTIST]
    audio["title"], audio["tracknumber"] = [f"{album} 1"], ["1"]
    audio["metadata_block_picture"] = [picture_text(*picture) for picture in pictures]
    audio.save()


class Corpus:
    def __init__(self, root, source):
        self.root = root
        self.source = source
        self.albums = []
        self.seed = 100

    def folder(self, name):
        path = self.root / name
        path.mkdir(parents=True, exist_ok=True)
        return path

    def art(self, width=600, height=600, noise=0.0):
        self.seed += 1
        return artwork(width, height, self.seed, noise)

    def add(self, name, candidates, expected, note=""):
        self.albums.append(Album(name, candidates, expected, note))

    def flac(self, name, pictures, expected, note=""):
        flac_track(self.folder(name) / "01.flac", self.source, name,
                   [(kind, mime, data) for kind, mime, data, _ in pictures])
        self.add(name, {str(index): reference for index, (_, _, _, reference)
                        in enumerate(pictures)}, expected, note)

    def flac_one(self, name, data, reference=None, mime="image/jpeg", note=""):
        shown = reference if reference is not None else decoded(data)
        self.flac(name, [(FRONT, mime, data, shown)], "0", note)


def jpeg_variants(corpus):
    image = corpus.art()
    corpus.flac_one("jpeg_baseline_420", jpeg(image, quality=90))
    corpus.flac_one("jpeg_baseline_422", jpeg(corpus.art(), quality=90, subsampling=1))
    corpus.flac_one("jpeg_baseline_444", jpeg(corpus.art(), quality=90, subsampling=0))
    corpus.flac_one("jpeg_progressive", jpeg(corpus.art(800, 800), quality=90, progressive=True))
    corpus.flac_one("jpeg_progressive_444",
                    jpeg(corpus.art(700, 600), quality=90, progressive=True, subsampling=0))
    corpus.flac_one("jpeg_gray", jpeg(corpus.art().convert("L"), quality=90))
    corpus.flac_one("jpeg_gray_progressive",
                    jpeg(corpus.art().convert("L"), quality=90, progressive=True))
    corpus.flac_one("jpeg_restart_markers", jpeg(corpus.art(640, 480), quality=90,
                                                 restart_marker_blocks=5))
    cmyk = cmyk_jpeg(corpus.art())
    corpus.flac_one("jpeg_cmyk", cmyk, decoded(cmyk).convert("RGB"))
    cmyk_progressive = cmyk_jpeg(corpus.art(), progressive=True)
    corpus.flac_one("jpeg_cmyk_progressive", cmyk_progressive,
                    decoded(cmyk_progressive).convert("RGB"))
    ycck_data = check_cover_decode.ycck_bytes(corpus.art())
    corpus.flac_one("jpeg_ycck", ycck_data, decoded(ycck_data).convert("RGB"))
    oriented = corpus.art(600, 400)
    for orientation in (3, 6, 8):
        data = with_orientation(oriented, orientation, quality=90)
        name = f"jpeg_exif_orientation_{orientation}"
        corpus.flac(name, [(FRONT, "image/jpeg", data, ImageOps.exif_transpose(decoded(data))),
                           ], "0", "shown upright")
        corpus.albums[-1].candidates["raw"] = decoded(data)


def size_variants(corpus):
    huge = corpus.art(3000, 3000, noise=0.4)
    huge_data = jpeg(huge, quality=93, subsampling=1)
    corpus.flac_one("jpeg_huge_3000", huge_data,
                    note=f"{len(huge_data) / 1e6:.1f} MB baseline")
    progressive = jpeg(corpus.art(3000, 3000, noise=0.3), quality=95, progressive=True)
    corpus.flac_one("jpeg_huge_progressive", progressive,
                    note=f"{len(progressive) / 1e6:.1f} MB progressive")
    corpus.flac_one("jpeg_wide_4000x1000", jpeg(corpus.art(4000, 1000), quality=90))
    corpus.flac_one("jpeg_tiny_16", jpeg(corpus.art(16, 16), quality=95))
    corpus.flac_one("jpeg_tiny_1", jpeg(corpus.art(1, 1), quality=95))
    big_png = corpus.art(2500, 2500, noise=0.2)
    corpus.flac_one("png_huge_2500", png(big_png), big_png, "image/png")


def png_variants(corpus):
    image = corpus.art(400, 400)
    corpus.flac_one("png_rgb", png(image), image, "image/png")
    rgba = corpus.art(300, 300).convert("RGBA")
    rgba.putalpha(Image.linear_gradient("L").resize((300, 300)))
    corpus.flac_one("png_rgba", png(rgba), rgba.convert("RGB"), "image/png")
    palette = corpus.art(300, 300).quantize(colors=64)
    corpus.flac_one("png_palette", png(palette), palette.convert("RGB"), "image/png")
    transparent = corpus.art(200, 200).quantize(colors=16)
    transparent.info["transparency"] = 0
    corpus.flac_one("png_palette_transparent", png(transparent, transparency=0),
                    transparent.convert("RGB"), "image/png")
    gray = corpus.art(300, 300).convert("L")
    corpus.flac_one("png_gray", png(gray), gray, "image/png")
    gray_alpha = corpus.art(250, 250).convert("LA")
    corpus.flac_one("png_gray_alpha", png(gray_alpha), gray_alpha.convert("RGB"), "image/png")
    deep = corpus.art(200, 200)
    corpus.flac_one("png_16_bit", sixteen_bit_png(deep), deep, "image/png")
    interlaced = corpus.art(257, 190)
    corpus.flac_one("png_interlaced", interlaced_png(interlaced), interlaced, "image/png")
    corpus.flac_one("png_tiny_1", png(corpus.art(1, 1)), None, "image/png")


def choice_variants(corpus):
    other, front, back = corpus.art(), corpus.art(), corpus.art()
    corpus.flac("flac_front_wins", [(OTHER, "image/jpeg", jpeg(other), other),
                                    (FRONT, "image/jpeg", jpeg(front), front),
                                    (BACK, "image/jpeg", jpeg(back), back)], "1")
    corpus.flac("flac_no_front_first", [(BACK, "image/jpeg", jpeg(back), back),
                                        (OTHER, "image/png", png(other), other)], "0")
    corpus.flac("flac_two_fronts_first", [(FRONT, "image/jpeg", jpeg(front), front),
                                          (FRONT, "image/jpeg", jpeg(other), other)], "0")


def id3_variants(corpus):
    source = corpus.source
    for version in (3, 4):
        art, back = corpus.art(), corpus.art()
        name = f"id3v2{version}_apic"
        mp3_track(corpus.folder(name) / "01.mp3", source, name,
                  [(BACK, "image/png", png(back)), (FRONT, "image/jpeg", jpeg(art))], version)
        corpus.add(name, {"back": back, "front": art}, "front")
    art = corpus.art()
    name = "id3v23_unsync"
    raw_mp3_track(corpus.folder(name) / "01.mp3", source,
                  v23_unsync_tag(name, (FRONT, "image/jpeg", jpeg(art))))
    corpus.add(name, {"front": art}, "front", "tag-wide unsynchronisation")
    art = corpus.art()
    name = "id3v24_frame_unsync"
    raw_mp3_track(corpus.folder(name) / "01.mp3", source,
                  v24_frame_unsync_tag(name, (FRONT, "image/jpeg", jpeg(art))))
    corpus.add(name, {"front": art}, "front", "frame unsynchronisation")


def mp4_variants(corpus):
    art, other = corpus.art(), corpus.art()
    name = "mp4_covr_jpeg"
    mp4_track(corpus.folder(name) / "01.m4a", corpus.source, name,
              [(MP4Cover.FORMAT_JPEG, jpeg(art))])
    corpus.add(name, {"0": art}, "0")
    name = "mp4_covr_png_two"
    mp4_track(corpus.folder(name) / "01.m4a", corpus.source, name,
              [(MP4Cover.FORMAT_PNG, png(art)), (MP4Cover.FORMAT_JPEG, jpeg(other))])
    corpus.add(name, {"0": art, "1": other}, "0")


def ogg_variants(corpus):
    art, other = corpus.art(), corpus.art()
    name = "ogg_vorbis_picture"
    ogg_track(corpus.folder(name) / "01.ogg", corpus.source, name,
              [(FRONT, "image/jpeg", jpeg(art))])
    corpus.add(name, {"0": art}, "0")
    large = corpus.art(1400, 1400, noise=0.5)
    data = jpeg(large, quality=96)
    name = "ogg_vorbis_large_picture"
    ogg_track(corpus.folder(name) / "01.ogg", corpus.source, name, [(FRONT, "image/jpeg", data)])
    corpus.add(name, {"0": large}, "0", f"{len(data) / 1e6:.1f} MB picture over many pages")
    name = "opus_front_second"
    ogg_track(corpus.folder(name) / "01.opus", corpus.source, name,
              [(BACK, "image/png", png(other)), (FRONT, "image/jpeg", jpeg(art))], opus=True)
    corpus.add(name, {"back": other, "front": art}, "front")


def folder_variants(corpus):
    for name, files, winner in (
            ("folder_Cover_JPG", ("Cover.JPG",), "Cover.JPG"),
            ("folder_folder_jpeg", ("folder.jpeg",), "folder.jpeg"),
            ("folder_FRONT_PNG", ("FRONT.PNG",), "FRONT.PNG"),
            ("folder_AlbumArt", ("AlbumArt.Jpg",), "AlbumArt.Jpg"),
            ("folder_cover_beats_folder", ("folder.jpg", "cover.png"), "cover.png"),
            ("folder_jpg_and_png", ("Cover.png", "cover.jpg"), "Cover.png")):
        folder = corpus.folder(name)
        flac_track(folder / "01.flac", corpus.source, name, [])
        candidates = {}
        for file_name in files:
            image = corpus.art()
            folder.joinpath(file_name).write_bytes(
                png(image) if file_name.lower().endswith(".png") else jpeg(image))
            candidates[file_name] = image
        corpus.add(name, candidates, winner)


def fallback_variants(corpus):
    good = corpus.art()
    progressive = jpeg(corpus.art(), quality=90, progressive=True)
    for name, embedded in (("fallback_truncated_jpeg", jpeg(corpus.art())[:9000]),
                           ("fallback_truncated_progressive",
                            progressive[:len(progressive) * 85 // 100]),
                           ("fallback_lossless_jpeg", unsupported_sof(jpeg(corpus.art()))),
                           ("fallback_gif", gif(corpus.art(200, 200))),
                           ("fallback_garbage", bytes(range(256)) * 40)):
        folder = corpus.folder(name)
        flac_track(folder / "01.flac", corpus.source, name, [(FRONT, "image/jpeg", embedded)])
        folder.joinpath("folder.jpg").write_bytes(jpeg(good))
        corpus.add(name, {"folder.jpg": good}, "folder.jpg", "embedded art unusable")
        good = corpus.art()
    name = "unusable_without_folder"
    flac_track(corpus.folder(name) / "01.flac", corpus.source, name,
               [(FRONT, "image/jpeg", jpeg(corpus.art())[:5000])])
    corpus.add(name, {}, None, "initials cover")
    name = "no_art"
    flac_track(corpus.folder(name) / "01.flac", corpus.source, name, [])
    corpus.add(name, {}, None, "initials cover")


def build_corpus(root, source, large=True):
    corpus = Corpus(root, source)
    jpeg_variants(corpus)
    if large:
        size_variants(corpus)
    png_variants(corpus)
    choice_variants(corpus)
    id3_variants(corpus)
    mp4_variants(corpus)
    ogg_variants(corpus)
    folder_variants(corpus)
    fallback_variants(corpus)
    return corpus.albums


def box_record(image):
    square = cover_export.square_crop(image.convert("RGB"))
    full = square.resize((FULL, FULL), Image.Resampling.BOX)
    half = square.resize((HALF, HALF), Image.Resampling.BOX)
    pixels = [cover_export.rgb565(*pixel) for pixel in list(full.getdata()) + list(half.getdata())]
    return struct.pack(f"<{len(pixels)}H", *pixels)


def expand(value):
    return ((value >> 11) * 255 // 31, (value >> 5 & 0x3F) * 255 // 63, (value & 0x1F) * 255 // 31)


def mean_difference(record, reference):
    count = len(record) // 2
    got = struct.unpack(f"<{count}H", record)
    expected = struct.unpack(f"<{count}H", reference)
    total = 0
    for left, right in zip(got, expected):
        total += sum(abs(a - b) for a, b in zip(expand(left), expand(right)))
    return total / (count * 3)


def identify(record, references):
    if record is None:
        return "not shown", None
    scored = sorted((mean_difference(record, reference), label)
                    for label, reference in references.items())
    if not scored or scored[0][0] > SHOWN_LIMIT:
        return "garbled", scored[0][0] if scored else None
    return scored[0][1], scored[0][0]


def read_covers(data):
    magic, version, count, full, half, _ = HEADER.unpack_from(data, 0)
    if magic != b"DSCV" or version != 1 or (full, half) != (FULL, HALF):
        raise ValueError("bad covers header")
    entries = [ENTRY.unpack_from(data, HEADER.size + index * ENTRY.size) for index in range(count)]
    return {key: data[offset:offset + RECORD_BYTES] for key, offset in entries}


def player_records(tool, music, output):
    result = subprocess.run([str(tool), str(music), str(output)], capture_output=True, text=True,
                            timeout=1200)
    if result.returncode != 0:
        raise RuntimeError(f"cover_build_tool exit {result.returncode}: {result.stderr}")
    print(result.stdout.strip())
    return read_covers((output / "covers.bin").read_bytes())


def echo_sync_records(music, output):
    sources = cover_export.album_sources_from_library((output / "library.db").read_bytes())
    covers = cover_export.collect_covers(music, sources, cover_export.cover_record)
    return covers.records


def outcome(label, expected):
    if label == "not shown":
        return "not shown" if expected is None else "MISSING"
    if label == "garbled":
        return "GARBLED"
    if expected is None:
        return f"UNEXPECTED {label}"
    return "shown" if label == expected else f"WRONG ({label})"


def evaluate(albums, player, pc):
    rows, failures = [], []
    for album in albums:
        key = cover_export.album_key(ARTIST, album.name)
        references = {label: box_record(image) for label, image in album.candidates.items()}
        sides = []
        for side, records in (("player", player), ("echo-sync", pc)):
            label, score = identify(records.get(key), references)
            result = outcome(label, album.expected)
            sides.append((result, label, score))
            if result not in ("shown", "not shown") and album.name not in PENDING:
                failures.append(f"{album.name}: {side} {result}")
        if sides[0][1] != sides[1][1] and album.name not in PENDING:
            failures.append(f"{album.name}: player chose {sides[0][1]}, echo-sync {sides[1][1]}")
        rows.append((album, sides))
    return rows, failures


def score_text(score):
    return "" if score is None else f" ({score:.1f})"


def print_table(rows):
    print(f"{'case':32} {'player':22} {'echo-sync':22} note")
    for album, sides in rows:
        cells = [f"{result}{score_text(score)}" for result, _, score in sides]
        print(f"{album.name:32} {cells[0]:22} {cells[1]:22} {album.note}")


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Check album art across formats, player and PC")
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--no-large", action="store_true")
    parser.add_argument("--table-only", action="store_true")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    shutil.rmtree(arguments.work, ignore_errors=True)
    music, output = arguments.work / "music", arguments.work / "output"
    output.mkdir(parents=True)
    albums = build_corpus(music, arguments.fixtures / "tags", not arguments.no_large)
    player = player_records(arguments.tool, music, output)
    pc = echo_sync_records(music, output)
    rows, failures = evaluate(albums, player, pc)
    print_table(rows)
    for failure in failures:
        print(f"FAIL {failure}", file=sys.stderr)
    return 0 if arguments.table_only or not failures else 1


if __name__ == "__main__":
    sys.exit(main())
