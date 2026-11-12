import argparse
import base64
import concurrent.futures
import csv
import io
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

SCRATCH = Path("/Volumes/Scratch/SnowskyEcho/data/test-library")
METADATA_MEMBER = "fma_metadata/tracks.csv"
FORMATS = (("mp3", 0.50), ("flac", 0.20), ("m4a", 0.15), ("ogg", 0.10), ("opus", 0.05))
ENCODERS = {
    "flac": ["-c:a", "flac", "-compression_level", "8"],
    "m4a": ["-c:a", "aac", "-b:a", "192k"],
    "ogg": ["-c:a", "vorbis", "-strict", "experimental", "-ac", "2", "-b:a", "160k"],
    "opus": ["-c:a", "libopus", "-b:a", "128k"],
}
ART_KINDS = (("baseline", 0.55), ("progressive", 0.20), ("png", 0.10), ("cmyk", 0.05),
             ("rotated", 0.05), ("none", 0.05))
FOLDER_NAMES = ("cover.jpg", "folder.jpg", "Cover.JPG", "front.png", "AlbumArt.jpg", "folder.jpeg")
FOLDER_SHARE = 0.25
FOLDER_ONLY_SHARE = 0.10
JOINED_SHARE = 0.01
JOINED_MINIMUM = 2
JOIN_MINIMUM = 3
ART_SIDES = (500, 1500)
FRONT_COVER = 3
UNSAFE = re.compile(r'[\\/:*?"<>|\x00-\x1f]')
NAME_LIMIT = 80
PART_PREFIX = ".part-"
SOURCE_PREFIX = ".source-"
WORK_PREFIX = ".work-"


@dataclass
class Track:
    track_id: int
    title: str
    number: int
    artist: str
    album_id: str
    album: str
    genre: str
    year: str
    license: str


@dataclass
class Album:
    album_id: str
    artist: str
    title: str
    tracks: list
    kind: str = "plain"
    format: str = "mp3"
    art: str = "baseline"
    folder_name: str = ""
    embedded: bool = True
    seed: int = 0
    files: list = field(default_factory=list)
    written: list = field(default_factory=list)
    skipped: list = field(default_factory=list)


def safe_name(text, fallback):
    cleaned = UNSAFE.sub("_", text or "").strip().rstrip(".")
    return (cleaned or fallback)[:NAME_LIMIT].rstrip(" .") or fallback


def allows_derivatives(license_text):
    folded = license_text.lower()
    return "noderiv" not in folded and "no deriv" not in folded and "no-deriv" not in folded


def weighted(generator, choices):
    point = generator.random()
    total = 0.0
    for name, weight in choices:
        total += weight
        if point < total:
            return name
    return choices[-1][0]


def read_catalog(metadata_zip):
    tracks = []
    with zipfile.ZipFile(metadata_zip) as archive, archive.open(METADATA_MEMBER) as raw:
        rows = csv.reader(io.TextIOWrapper(raw, encoding="utf-8", newline=""))
        first, second = next(rows), next(rows)
        next(rows)
        index = {"%s.%s" % pair: position for position, pair in enumerate(zip(first, second))}
        for row in rows:
            if row[index["set.subset"]] != "small":
                continue
            number = row[index["track.number"]]
            tracks.append(Track(int(row[0]), row[index["track.title"]],
                                int(number) if number.isdigit() else 0,
                                row[index["artist.name"]], row[index["album.id"]],
                                row[index["album.title"]], row[index["track.genre_top"]],
                                row[index["album.date_released"]][:4], row[index["track.license"]]))
    return tracks


def album_order(tracks, seed):
    grouped = {}
    for track in tracks:
        grouped.setdefault(track.album_id or "single-%d" % track.track_id, []).append(track)
    keys = sorted(grouped)
    random.Random(seed).shuffle(keys)
    return [(key, sorted(grouped[key], key=lambda track: (track.number, track.track_id)))
            for key in keys]


def take_albums(order, start, needed, seed):
    albums, total = [], 0
    for key, members in order[start:]:
        if total >= needed:
            break
        first = members[0]
        albums.append(Album(key, first.artist, first.album or first.title, members,
                            seed=seed * 100003 + start + len(albums)))
        total += len(members)
    return albums


def plan_albums(tracks, count, seed):
    albums = take_albums(album_order(tracks, seed), 0, count, seed)
    choose_joined(albums)
    for album in albums:
        decide(album, random.Random(album.seed))
    return albums


def more_albums(tracks, seed, planned, needed):
    albums = take_albums(album_order(tracks, seed), len(planned), needed, seed)
    for album in albums:
        decide(album, random.Random(album.seed))
    return albums


def joinable(album):
    return len(album.tracks) >= JOIN_MINIMUM and all(
        allows_derivatives(track.license) for track in album.tracks)


def choose_joined(albums):
    wanted = max(JOINED_MINIMUM, int(len(albums) * JOINED_SHARE))
    candidates = [album for album in albums if joinable(album)]
    for index, album in enumerate(candidates[:2 * wanted]):
        album.kind = "cue" if index % 2 == 0 else "book"


def decide(album, generator):
    album.format = {"cue": "flac", "book": "m4b"}.get(album.kind) or weighted(generator, FORMATS)
    album.art = weighted(generator, ART_KINDS)
    folder_roll = generator.random()
    if album.art != "none" and folder_roll < FOLDER_SHARE:
        album.folder_name = FOLDER_NAMES[generator.randrange(len(FOLDER_NAMES))]
        album.embedded = folder_roll >= FOLDER_ONLY_SHARE
    if album.art == "none":
        album.embedded = False


def artwork(seed):
    from PIL import Image, ImageDraw
    generator = random.Random(seed)
    side = generator.randint(*ART_SIDES)
    colors = [tuple(generator.randrange(256) for _ in range(3)) for _ in range(5)]
    image = Image.linear_gradient("L").resize((side, side)).convert("RGB")
    image = Image.blend(Image.new("RGB", (side, side), colors[0]), image, 0.4)
    draw = ImageDraw.Draw(image)
    for index in range(generator.randint(8, 24)):
        left, top = generator.randrange(side), generator.randrange(side)
        size = side * generator.randint(5, 35) // 100
        shape = draw.ellipse if index % 2 else draw.rectangle
        shape((left - size, top - size, left + size, top + size // 2), fill=colors[1 + index % 4])
    grain = Image.frombytes("RGB", (side, side), generator.randbytes(side * side * 3))
    return Image.blend(image, grain, generator.uniform(0.02, 0.15))


def encoded_art(album):
    from PIL import Image
    image = artwork(album.seed)
    buffer = io.BytesIO()
    if album.art == "png":
        image.quantize(128).convert("RGB").save(buffer, "PNG")
        return buffer.getvalue(), "image/png"
    if album.art == "cmyk":
        image.convert("CMYK").save(buffer, "JPEG", quality=90)
    elif album.art == "rotated":
        exif = Image.Exif()
        exif[0x0112] = 6
        image.transpose(Image.Transpose.ROTATE_90).save(buffer, "JPEG", quality=88,
                                                         exif=exif.tobytes())
    else:
        image.save(buffer, "JPEG", quality=random.Random(album.seed).randint(82, 94),
                   progressive=album.art == "progressive")
    return buffer.getvalue(), "image/jpeg"


def source_member(track_id):
    return "fma_small/%03d/%06d.mp3" % (track_id // 1000, track_id)


def album_folder(root, album):
    artist = safe_name(album.artist, "Unknown Artist")
    title = safe_name(album.title, "Unknown Album")
    base = root / "Audiobooks" / artist if album.kind == "book" else root / artist
    return base / title


def readable(path):
    from mutagen.mp3 import MP3
    try:
        MP3(path)
    except Exception:
        return False
    return True


def finished(path):
    import mutagen
    try:
        audio = mutagen.File(path, easy=True)
    except Exception:
        return False
    return audio is not None and bool(audio.get("title"))


def clean_leftovers(root):
    for path in sorted(root.rglob("*"), reverse=True):
        if path.name.startswith(WORK_PREFIX) and path.is_dir():
            shutil.rmtree(path)
        elif path.name.startswith((PART_PREFIX, SOURCE_PREFIX)) and path.is_file():
            path.unlink()


def run_ffmpeg(arguments):
    result = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *arguments],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("ffmpeg failed: %s" % result.stderr.strip()[:400])


def tag_file(path, kind, values, picture):
    from mutagen.flac import FLAC, Picture
    from mutagen.mp4 import MP4, MP4Cover
    from mutagen.oggopus import OggOpus
    from mutagen.oggvorbis import OggVorbis
    if kind == "mp3":
        tag_mp3(path, values, picture)
        return
    if kind in ("m4a", "m4b"):
        audio = MP4(path)
        audio.delete()
        audio["\xa9nam"], audio["\xa9ART"], audio["aART"] = [values["title"]], [values["artist"]], [values["artist"]]
        audio["\xa9alb"], audio["\xa9gen"], audio["\xa9day"] = [values["album"]], [values["genre"]], [values["year"]]
        audio["trkn"] = [(values["number"], 0)]
        audio["cprt"] = [values["license"]]
        if picture is not None:
            kind_code = MP4Cover.FORMAT_PNG if picture[1] == "image/png" else MP4Cover.FORMAT_JPEG
            audio["covr"] = [MP4Cover(picture[0], imageformat=kind_code)]
        audio.save()
        return
    audio = {"flac": FLAC, "ogg": OggVorbis, "opus": OggOpus}[kind](path)
    audio.delete()
    for key in ("title", "artist", "album", "genre"):
        audio[key] = values[key]
    audio["albumartist"], audio["date"] = values["artist"], values["year"]
    audio["tracknumber"], audio["license"] = str(values["number"]), values["license"]
    if picture is not None:
        block = Picture()
        block.type, block.mime, block.data = FRONT_COVER, picture[1], picture[0]
        if kind == "flac":
            audio.clear_pictures()
            audio.add_picture(block)
        else:
            audio["metadata_block_picture"] = [base64.b64encode(block.write()).decode("ascii")]
    audio.save()


def tag_mp3(path, values, picture):
    from mutagen import id3
    tags = id3.ID3()
    for frame in (id3.TIT2(encoding=3, text=values["title"]), id3.TPE1(encoding=3, text=values["artist"]),
                  id3.TPE2(encoding=3, text=values["artist"]), id3.TALB(encoding=3, text=values["album"]),
                  id3.TCON(encoding=3, text=values["genre"]), id3.TDRC(encoding=3, text=values["year"]),
                  id3.TRCK(encoding=3, text=str(values["number"])), id3.TCOP(encoding=3, text=values["license"])):
        tags.add(frame)
    if picture is not None:
        tags.add(id3.APIC(encoding=0, mime=picture[1], type=FRONT_COVER, desc="", data=picture[0]))
    tags.save(path, v2_version=3 if values["number"] % 2 else 4)


def track_values(album, track, position):
    return {"title": track.title or "Track %d" % position, "artist": album.artist or "Unknown Artist",
            "album": album.title or "Unknown Album", "genre": track.genre or "Unknown",
            "year": track.year or "", "number": position, "license": track.license}


def write_source(archive, album, track, target):
    part = target.with_name(PART_PREFIX + target.name)
    with tempfile.NamedTemporaryFile(prefix=SOURCE_PREFIX, suffix=".mp3", dir=target.parent) as source:
        source.write(archive.read(source_member(track.track_id)))
        source.flush()
        if not readable(source.name):
            raise ValueError("no MPEG audio frames")
        if album.format == "mp3":
            shutil.copyfile(source.name, part)
        else:
            run_ffmpeg(["-i", source.name, "-map", "0:a", "-map_metadata", "-1",
                        *ENCODERS[album.format], str(part)])


def write_plain(archive_path, root, album, picture):
    folder = album_folder(root, album)
    folder.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive_path) as archive:
        for position, track in enumerate(album.tracks, 1):
            name = "%02d %s.%s" % (position, safe_name(track.title, "Track"), album.format)
            target = folder / name
            if not finished(target):
                try:
                    write_source(archive, album, track, target)
                except (ValueError, RuntimeError) as problem:
                    target.with_name(PART_PREFIX + target.name).unlink(missing_ok=True)
                    target.unlink(missing_ok=True)
                    album.skipped.append((track.track_id, str(problem).splitlines()[0][:200]))
                    continue
                part = target.with_name(PART_PREFIX + target.name)
                tag_file(part, album.format, track_values(album, track, position), picture)
                part.replace(target)
            album.files.append((target, track))
            album.written.append(track)
    return folder


class Unreadable(Exception):
    def __init__(self, skipped):
        super().__init__("unreadable parts")
        self.skipped = skipped


def joined_audio(archive_path, folder, album, target, codec):
    from mutagen.mp3 import MP3
    lengths = []
    with zipfile.ZipFile(archive_path) as archive, \
            tempfile.TemporaryDirectory(prefix=WORK_PREFIX, dir=folder) as work:
        parts, unreadable = [], []
        for index, track in enumerate(album.tracks):
            part = Path(work) / ("%03d.mp3" % index)
            part.write_bytes(archive.read(source_member(track.track_id)))
            if not readable(part):
                unreadable.append((track.track_id, "no MPEG audio frames"))
                continue
            lengths.append(MP3(part).info.length)
            parts.append(part)
        if unreadable:
            raise Unreadable(unreadable)
        listing = Path(work) / "parts.txt"
        listing.write_text("".join("file '%s'\n" % part.name for part in parts))
        chapters = Path(work) / "chapters.txt"
        chapters.write_text(chapter_text(album, lengths))
        run_ffmpeg(["-f", "concat", "-safe", "0", "-i", str(listing), "-i", str(chapters),
                    "-map", "0:a", "-map_metadata", "-1", "-map_chapters", "1", *codec, str(target)])
    return lengths


def chapter_text(album, lengths):
    lines, start = [";FFMETADATA1"], 0
    for track, length in zip(album.tracks, lengths):
        end = start + int(length * 1000)
        lines += ["[CHAPTER]", "TIMEBASE=1/1000", "START=%d" % start, "END=%d" % end,
                  "title=%s" % (track.title or "Chapter").replace("=", "-").replace("\n", " ")]
        start = end
    return "\n".join(lines) + "\n"


def cue_text(album, file_name, lengths):
    lines = ['PERFORMER "%s"' % album.artist.replace('"', "'"), 'TITLE "%s"' % album.title.replace('"', "'"),
             'FILE "%s" WAVE' % file_name]
    start = 0.0
    for number, (track, length) in enumerate(zip(album.tracks, lengths), 1):
        frames = int(round(start * 75))
        lines += ["  TRACK %02d AUDIO" % number, '    TITLE "%s"' % (track.title or "Track").replace('"', "'"),
                  "    INDEX 01 %02d:%02d:%02d" % (frames // 4500, frames // 75 % 60, frames % 75)]
        start += length
    return "\r\n".join(lines) + "\r\n"


def prune(folder, root):
    while folder != root and folder.is_dir() and not any(folder.iterdir()):
        folder.rmdir()
        folder = folder.parent


def joined_target(root, album):
    folder = album_folder(root, album)
    stem = safe_name(album.title, "Album")
    if album.kind == "cue":
        return folder, folder / (stem + ".flac"), folder / (stem + ".cue")
    return folder, folder.parent / (stem + ".m4b"), None


def write_joined(archive_path, root, album, picture):
    folder, target, cue = joined_target(root, album)
    kind = "flac" if cue else "m4b"
    if not finished(target) or (cue is not None and not cue.exists()):
        folder.mkdir(parents=True, exist_ok=True)
        part = target.with_name(PART_PREFIX + target.name)
        codec = ENCODERS["flac"] if cue else ENCODERS["m4a"] + ["-f", "mp4"]
        try:
            lengths = joined_audio(archive_path, folder, album, part, codec)
        except (Unreadable, RuntimeError) as problem:
            part.unlink(missing_ok=True)
            album.skipped += getattr(problem, "skipped", None) or [
                (track.track_id, str(problem).splitlines()[0][:200]) for track in album.tracks]
            prune(folder, root)
            return None
        tag_file(part, kind, track_values(album, album.tracks[0], 1) | {"title": album.title}, picture)
        part.replace(target)
        if cue is not None:
            cue.write_bytes(cue_text(album, target.name, lengths).encode("utf-8"))
    album.files.append((target, album.tracks[0]))
    album.written = list(album.tracks)
    if cue is None:
        prune(folder, root)
        return target.parent
    return folder


def write_album(archive_path, root, album):
    art = encoded_art(album) if album.art != "none" else None
    picture = art if album.embedded else None
    writer = write_joined if album.kind != "plain" else write_plain
    folder = writer(archive_path, root, album, picture)
    if album.written and album.folder_name and art is not None:
        (folder / album.folder_name).write_bytes(art[0])
    elif not album.written and folder is not None:
        prune(folder, root)
    return album


def manifest(albums, root, count):
    skipped = [item for album in albums for item in album.skipped]
    files = [(path, track, album) for album in albums for path, track in album.files]
    by_format, art = {}, {}
    albums = [album for album in albums if album.written]
    for album in albums:
        by_format[album.format] = by_format.get(album.format, 0) + len(album.written)
        art[album.art] = art.get(album.art, 0) + 1
    return {
        "source": "Free Music Archive, fma_small (Creative Commons; private testing only)",
        "requested_tracks": count,
        "summary": {
            "tracks": sum(len(album.written) for album in albums), "albums": len(albums),
            "files": len(files), "tracks_by_format": by_format, "art_kinds": art,
            "albums_with_folder_images": sum(1 for album in albums if album.folder_name),
            "albums_with_only_folder_images": sum(1 for album in albums
                                                  if album.folder_name and not album.embedded),
            "cue_albums": sum(1 for album in albums if album.kind == "cue"),
            "books": sum(1 for album in albums if album.kind == "book"),
            "bytes": sum(path.stat().st_size for path, _, _ in files),
            "skipped_tracks": len(skipped),
        },
        "skipped": [{"fma_track_id": track_id, "reason": reason} for track_id, reason in skipped],
        "tracks": [{"path": str(path.relative_to(root)), "fma_track_id": track.track_id,
                    "artist": album.artist, "album": album.title, "title": track.title,
                    "license": track.license, "format": album.format, "art": album.art}
                   for path, track, album in files],
    }


def credits(albums):
    lines = ["Music from the Free Music Archive (https://freemusicarchive.org), fma_small set.",
             "Each track keeps its own Creative Commons licence; for private testing only.", ""]
    for album in albums:
        for track in album.written:
            lines.append("%06d  %s - %s (%s): %s" % (track.track_id, album.artist, track.title,
                                                     album.title, track.license))
    return "\n".join(lines) + "\n"


def write_albums(archive_path, out, albums, workers, log):
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        futures = [pool.submit(write_album, archive_path, out, album) for album in albums]
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            for track_id, reason in future.result().skipped:
                log("  skipped FMA track %06d: %s" % (track_id, reason))
            if done % 100 == 0:
                log("  %d of %d albums written" % (done, len(albums)))


def build(fma, out, count, seed, workers, log=print):
    catalog = read_catalog(fma / "fma_metadata.zip")
    albums = plan_albums(catalog, count, seed)
    log("planned %d albums, %d tracks" % (len(albums), sum(len(album.tracks) for album in albums)))
    out.mkdir(parents=True, exist_ok=True)
    clean_leftovers(out)
    archive_path = fma / "fma_small.zip"
    batch = albums
    while batch:
        write_albums(archive_path, out, batch, workers, log)
        short = count - sum(len(album.written) for album in albums)
        batch = more_albums(catalog, seed, albums, short) if short > 0 else []
        albums += batch
        if batch:
            log("%d tracks short; adding %d albums" % (short, len(batch)))
    (out / "CREDITS.txt").write_text(credits(albums))
    data = manifest(albums, out, count)
    (out.parent / (out.name + "-manifest.json")).write_text(json.dumps(data, ensure_ascii=False, indent=1))
    return data


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Build a realistic card layout from the FMA "
                                                 "Creative Commons set for player timing tests; "
                                                 "a rerun keeps finished files")
    parser.add_argument("--fma", type=Path, default=Path("/Volumes/Scratch/SnowskyEcho/data/fma"))
    parser.add_argument("--tracks", type=int, default=4000)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--workers", type=int, default=4)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    out = arguments.out or SCRATCH / ("fma-%d" % arguments.tracks)
    data = build(arguments.fma, out, arguments.tracks, arguments.seed, arguments.workers)
    print(json.dumps(data["summary"], indent=1))
    print("music: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
