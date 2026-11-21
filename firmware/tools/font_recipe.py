"""Builds extra.font, the glyphs the player's built-in fonts lack, on the computer.

The recipe (assets/fonts/recipe.json) pins the Noto Sans CJK fonts, maps languages to a style and
lists the characters every face can render. The input characters come from the language packs on
the music volume, the library database and missing.txt; the output is one file on internal
storage (docs/architecture.md, Languages).
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import time
import unicodedata
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import ImageFont

import font_build
import glyph_pack
import lang_pack
import library_index
from identity import IDENTITY

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "assets" / "fonts"
RECIPE = FONTS / "recipe.json"
RECIPE_VERSION = 1
STYLES = ("sc", "tc", "jp", "kr")
WEIGHTS = ("regular", "bold")
DEFAULT = "default"
SOURCE_FIELDS = ("file", "url", "sha256", "size", "font_number")
FACE_FIELDS = ("name", "size", "weight", "max_bitmap")
DATA_FOLDER = IDENTITY.data_directory
FONTS_FOLDER = "fonts"
EXTRA_FILE = "extra.font"
MISSING_FILE = "missing.txt"
LANG_FOLDER = "lang"
PACK_SUFFIX = ".pack"
LIBRARY_FILE = "library.db"
SETTINGS_FILE = "settings.bin"
STAGE_SUFFIX = ".part"
SETTINGS_MAGIC = b"DSST"
SETTINGS_LANGUAGE = slice(19, 24)
LIBRARY_SECTIONS = 10
STRINGS_SECTION = 9
MEBIBYTE = 1024 * 1024
LIBRARY_LIMIT = 128 * MEBIBYTE
MISSING_LIMIT = MEBIBYTE
SETTINGS_LIMIT = 4096
DIRECTORY_LIMIT = 256
MAX_LANGUAGES = 32
MAX_CODEPOINT = 0xFFFF
FREE_MARGIN = MEBIBYTE
CHUNK = MEBIBYTE
DOWNLOAD_TIMEOUT = 60
NEVER_DRAWN = {"Cc", "Cs"}


class RecipeError(Exception):
    pass


@dataclass(frozen=True)
class Volumes:
    internal: Path
    music: Path | None = None

    @property
    def library_volume(self):
        return self.music or self.internal


@dataclass
class Extras:
    language: str | None = None
    packs: dict = field(default_factory=dict)
    library: bytes | None = None
    texts: tuple = ()


@dataclass
class Options:
    fonts_dir: Path | None = None
    dry_run: bool = False
    cache: Path | None = None
    fetch: object = None


@dataclass
class Setup:
    recipe: dict
    drawable: frozenset


@dataclass
class Plan:
    style: str
    characters: list
    unrenderable: list
    languages: list


@dataclass
class Report:
    path: Path
    action: str
    style: str
    characters: int
    unrenderable: list
    languages: list
    size: int
    seconds: float
    skipped: list = field(default_factory=list)

    def as_dict(self):
        return {"path": str(self.path), "action": self.action, "style": self.style,
                "characters": self.characters, "size": self.size, "languages": self.languages,
                "unrenderable": "".join(chr(codepoint) for codepoint in self.unrenderable),
                "unrenderable_count": len(self.unrenderable), "seconds": round(self.seconds, 3),
                "skipped": self.skipped}

    def summary(self):
        text = {"written": f"{self.characters:,} characters, {self.size:,} bytes",
                "unchanged": f"{self.characters:,} characters, unchanged",
                "removed": "none needed, the old file was removed",
                "absent": "none needed",
                "would write": f"would write {self.characters:,} characters, {self.size:,} bytes",
                "would remove": "none needed, would remove the old file"}[self.action]
        if self.unrenderable:
            text += f"; {len(self.unrenderable):,} characters can't be drawn"
        return text


def ranges_of(codepoints):
    ranges = []
    for codepoint in sorted(set(codepoints)):
        if ranges and ranges[-1][1] == codepoint - 1:
            ranges[-1][1] = codepoint
        else:
            ranges.append([codepoint, codepoint])
    return ranges


def range_set(ranges):
    return frozenset(codepoint for first, last in ranges for codepoint in range(first, last + 1))


def check_source(source, where):
    if sorted(source) != sorted(SOURCE_FIELDS):
        raise RecipeError(f"{where} must have exactly {', '.join(SOURCE_FIELDS)}")
    if (not isinstance(source["file"], str) or "/" in source["file"] or "\\" in source["file"] or
            source["file"].startswith(".") or not source["url"].startswith("https://") or
            len(source["sha256"]) != 64 or not isinstance(source["size"], int) or
            not isinstance(source["font_number"], int) or source["font_number"] < 0):
        raise RecipeError(f"{where} has an invalid field")


def check_recipe(recipe):
    """Raises RecipeError unless recipe has the version 1 shape."""
    if recipe.get("version") != RECIPE_VERSION:
        raise RecipeError(f"the recipe is not version {RECIPE_VERSION}")
    if sorted(recipe.get("sources", {})) != sorted(STYLES):
        raise RecipeError(f"the recipe needs sources for {', '.join(STYLES)}")
    for style in STYLES:
        if sorted(recipe["sources"][style]) != sorted(WEIGHTS):
            raise RecipeError(f"the {style} sources need {', '.join(WEIGHTS)}")
        for weight in WEIGHTS:
            check_source(recipe["sources"][style][weight], f"source {style} {weight}")
    styles = recipe.get("styles", {})
    if styles.get(DEFAULT) not in STYLES or any(value not in STYLES for value in styles.values()):
        raise RecipeError("every language must map to a known style, with a default")
    faces = recipe.get("faces", [])
    if len(faces) != glyph_pack.EXTRA_FACES or any(sorted(face) != sorted(FACE_FIELDS) or
                                                   face["weight"] not in WEIGHTS for face in faces):
        raise RecipeError(f"the recipe needs {glyph_pack.EXTRA_FACES} faces")
    coverage = recipe.get("coverage", [])
    if any(not 0 < first <= last <= MAX_CODEPOINT for first, last in coverage) or any(
            coverage[index][1] + 1 >= coverage[index + 1][0] for index in range(len(coverage) - 1)):
        raise RecipeError("coverage must be sorted, separate BMP ranges")
    return recipe


def load_recipe(path=RECIPE):
    return check_recipe(json.loads(Path(path).read_text(encoding="utf-8")))


def base_drawable(fonts, packs):
    """The characters every text size draws without the CJK fallback faces."""
    own = {**fonts, "fonts": [{key: value for key, value in font.items() if key != "fallback_faces"}
                              for font in fonts["fonts"]]}
    return frozenset(lang_pack.drawable_from_config(own, packs))


def default_setup(recipe=RECIPE):
    fonts = json.loads((FONTS / "fonts.json").read_text(encoding="utf-8"))
    packs = json.loads((FONTS / "glyph_pack.json").read_text(encoding="utf-8"))
    return Setup(load_recipe(recipe), base_drawable(fonts, packs))


def style_for(recipe, language):
    styles = {code.lower(): style for code, style in recipe["styles"].items()}
    code = (language or "").lower()
    return styles.get(code) or styles.get(code.split("-")[0]) or styles[DEFAULT]


def drawn_characters(text):
    return {ord(character) for character in text if unicodedata.category(character) not in NEVER_DRAWN}


def pack_strings(data):
    """The code and texts of a language pack, without needing its catalogue."""
    info = lang_pack.pack_info(data)
    texts = []
    for segment in info["segments"]:
        block = data[segment["offset"]:segment["offset"] + segment["size"]]
        body = block[2 * segment["count"]:]
        texts += [text.decode("utf-8") for text in body.split(b"\0") if text]
    return info["code"], texts


def library_strings(data):
    """Every string in a library database: titles, names, paths and playlists."""
    prefix = library_index.HEADER_PREFIX
    if len(data) < library_index.HEADER_SIZE or prefix.unpack_from(data) != (
            library_index.MAGIC, library_index.VERSION, LIBRARY_SECTIONS):
        raise RecipeError("the library database is not a current one")
    offset, count, stride, _ = library_index.SECTION_ENTRY.unpack_from(
        data, prefix.size + STRINGS_SECTION * library_index.SECTION_ENTRY.size)
    if stride != 1 or offset < library_index.HEADER_SIZE or offset + count > len(data):
        raise RecipeError("the library database's strings are damaged")
    return [text.decode("utf-8", "replace") for text in data[offset:offset + count].split(b"\0") if text]


def read_regular(path, limit):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(descriptor, "rb") as file:
        info = os.fstat(file.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise RecipeError(f"{path} is not a regular file of at most {limit:,} bytes")
        return file.read(limit + 1)[:limit]


def optional_file(path, limit):
    try:
        return read_regular(path, limit)
    except (FileNotFoundError, NotADirectoryError):
        return None


def volume_packs(music, skipped):
    folder = Path(music) / DATA_FOLDER / LANG_FOLDER
    try:
        names = sorted(os.listdir(folder))
    except (FileNotFoundError, NotADirectoryError):
        return {}
    packs = {}
    for name in names[:DIRECTORY_LIMIT]:
        if name.startswith(".") or not name.endswith(PACK_SUFFIX):
            continue
        try:
            code, texts = pack_strings(read_regular(folder / name, lang_pack.FILE_LIMIT))
        except (lang_pack.PackError, RecipeError, UnicodeDecodeError, OSError) as error:
            skipped.append(f"{name}: {error}")
            continue
        if code == name[:-len(PACK_SUFFIX)]:
            packs[code] = texts
    return packs


def saved_language(internal):
    data = optional_file(Path(internal) / DATA_FOLDER / SETTINGS_FILE, SETTINGS_LIMIT)
    if data is None or len(data) < SETTINGS_LANGUAGE.stop or data[:4] != SETTINGS_MAGIC:
        return None
    code = data[SETTINGS_LANGUAGE].split(b"\0")[0]
    return code.decode("ascii", "replace") or None


def gather(volumes, extras, skipped):
    """The language packs and other texts the player shows, read from the volumes."""
    packs = volume_packs(volumes.library_volume, skipped)
    for data in extras.packs.values():
        code, texts = pack_strings(data)
        packs[code] = texts
    texts = list(extras.texts)
    library = extras.library
    if library is None:
        library = optional_file(volumes.library_volume / DATA_FOLDER / LIBRARY_FILE, LIBRARY_LIMIT)
    if library is not None:
        try:
            texts += library_strings(library)
        except RecipeError as error:
            skipped.append(f"{LIBRARY_FILE}: {error}")
    missing = optional_file(Path(volumes.internal) / DATA_FOLDER / FONTS_FOLDER / MISSING_FILE,
                            MISSING_LIMIT)
    if missing is not None:
        texts.append(missing.decode("utf-8", "replace"))
    return packs, texts


def plan(setup, packs, texts, language):
    """Which characters extra.font holds, which can't be drawn and which languages it covers."""
    wanted = set()
    for text in [*texts, *(text for values in packs.values() for text in values)]:
        wanted |= drawn_characters(text)
    needed = wanted - setup.drawable
    coverage = range_set(setup.recipe["coverage"])
    extra = sorted(codepoint for codepoint in needed if codepoint <= MAX_CODEPOINT and codepoint in coverage)
    return Plan(style_for(setup.recipe, language), extra, sorted(needed - set(extra)),
                covered_languages(packs, setup.drawable | set(extra)))


def covered_languages(packs, drawable):
    covered = [code for code, texts in sorted(packs.items())
               if all(drawn_characters(text) <= drawable for text in texts)]
    return covered[:MAX_LANGUAGES]


def cache_directory(platform=sys.platform, environ=os.environ, home=None):
    home = Path(home or Path.home())
    if platform == "darwin":
        base = home / "Library" / "Caches"
    elif platform.startswith("win"):
        base = Path(environ.get("LOCALAPPDATA") or home / "AppData" / "Local")
    else:
        base = Path(environ.get("XDG_CACHE_HOME") or home / ".cache")
    return base / IDENTITY.name / FONTS_FOLDER


def file_digest(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        while chunk := file.read(CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def verified(path, source):
    return path.is_file() and path.stat().st_size == source["size"] and file_digest(path) == source["sha256"]


def open_url(url):
    return urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT)


def download(source, target, fetch):
    target.parent.mkdir(parents=True, exist_ok=True)
    stage = target.with_name(target.name + STAGE_SUFFIX)
    digest, size = hashlib.sha256(), 0
    try:
        with fetch(source["url"]) as response, open(stage, "wb") as output:
            while chunk := response.read(CHUNK):
                size += len(chunk)
                if size > source["size"]:
                    break
                digest.update(chunk)
                output.write(chunk)
        if size != source["size"] or digest.hexdigest() != source["sha256"]:
            raise RecipeError(f"the download of {source['file']} doesn't match the recipe's SHA-256")
        os.replace(stage, target)
    finally:
        stage.unlink(missing_ok=True)


def font_file(source, options):
    """The verified local copy of a pinned font, downloaded into the cache on first use."""
    if options.fonts_dir is not None:
        path = Path(options.fonts_dir) / source["file"]
        if not verified(path, source):
            raise RecipeError(f"{path} is missing or isn't the pinned {source['file']} "
                              f"(SHA-256 {source['sha256']})")
        return path
    path = Path(options.cache or cache_directory()) / source["file"]
    if verified(path, source):
        return path
    try:
        download(source, path, options.fetch or open_url)
    except (urllib.error.URLError, OSError) as error:
        raise RecipeError(
            f"couldn't download {source['file']} ({source['size'] / MEBIBYTE:.1f} MB) from "
            f"{source['url']}: {getattr(error, 'reason', None) or error}. Connect to the internet "
            f"and try again, or download it yourself and pass --fonts-dir with its folder") from error
    return path


def render_face(path, spec, font_number, characters):
    font = ImageFont.truetype(str(path), spec["size"], index=font_number)
    glyphs, dropped = [], set()
    for codepoint in characters:
        glyph = font_build.render_glyph(font, codepoint)
        try:
            glyph_pack.check_glyph(glyph)
        except glyph_pack.PackError:
            dropped.add(codepoint)
            continue
        if (glyph.width * glyph.height + 1) // 2 > spec["max_bitmap"]:
            dropped.add(codepoint)
        glyphs.append(glyph)
    return glyphs, dropped


def render(setup, style, characters, options):
    """The seven face blobs for characters, and the characters some face had to drop."""
    sources = setup.recipe["sources"][style]
    paths = {weight: font_file(sources[weight], options) for weight in WEIGHTS}
    rendered, dropped = [], set()
    for spec in setup.recipe["faces"]:
        glyphs, lost = render_face(paths[spec["weight"]], spec, sources[spec["weight"]]["font_number"],
                                   characters)
        rendered.append(glyphs)
        dropped |= lost
    faces = [glyph_pack.pack_glyphs([glyph for glyph in glyphs if glyph.codepoint not in dropped])
             if set(characters) - dropped else None for glyphs in rendered]
    return faces, dropped


def build(setup, packs, texts, language, options):
    """The extra.font bytes (None when nothing is needed) and the plan they follow."""
    result = plan(setup, packs, texts, language)
    if not result.characters:
        return None, result
    faces, dropped = render(setup, result.style, result.characters, options)
    if dropped:
        kept = [codepoint for codepoint in result.characters if codepoint not in dropped]
        result = Plan(result.style, kept, sorted(set(result.unrenderable) | dropped),
                      covered_languages(packs, setup.drawable | set(kept)))
    if not result.characters:
        return None, result
    return glyph_pack.pack_extra_font(faces, result.languages), result


def extra_path(internal):
    return Path(internal) / DATA_FOLDER / FONTS_FOLDER / EXTRA_FILE


def checked_folder(internal):
    data = Path(internal) / DATA_FOLDER
    folder = data / FONTS_FOLDER
    for path in (data, folder):
        if path.is_symlink():
            raise RecipeError(f"{path} is a symbolic link; nothing was written")
    if not data.is_dir():
        raise RecipeError(f"{internal} has no {DATA_FOLDER} folder: it isn't the player's internal storage")
    return folder


def sync_directory(folder):
    if hasattr(os, "O_DIRECTORY"):
        descriptor = os.open(folder, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def write_stage(stage, data):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)
    descriptor = os.open(stage, flags, 0o644)
    try:
        view = memoryview(data)
        while view:
            view = view[os.write(descriptor, view):]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def current_bytes(path):
    try:
        return read_regular(path, max(path.stat().st_size, 1))
    except (FileNotFoundError, NotADirectoryError):
        return None


def install(internal, data, dry_run=False):
    """Writes or removes extra.font atomically; touches no other file. Returns the action."""
    folder = checked_folder(internal)
    path, stage = folder / EXTRA_FILE, folder / (EXTRA_FILE + STAGE_SUFFIX)
    existing = current_bytes(path) if folder.is_dir() else None
    if data is None:
        if existing is None:
            return "absent"
        if dry_run:
            return "would remove"
        path.unlink()
        sync_directory(folder)
        return "removed"
    if existing == data:
        return "unchanged"
    if dry_run:
        return "would write"
    folder.mkdir(exist_ok=True)
    free = shutil.disk_usage(folder).free
    if free < len(data) + FREE_MARGIN:
        raise RecipeError(f"{internal} has {free:,} bytes free; {EXTRA_FILE} needs {len(data):,}")
    stage.unlink(missing_ok=True)
    try:
        write_stage(stage, data)
        os.replace(stage, path)
    finally:
        stage.unlink(missing_ok=True)
    sync_directory(folder)
    return "written"


@dataclass
class Prepared:
    data: bytes | None
    plan: Plan
    skipped: list
    started: float


def prepare(volumes, extras=None, options=None, setup=None):
    """Gathers the characters and builds extra.font without writing anything."""
    started = time.monotonic()
    extras, options = extras or Extras(), options or Options()
    setup = setup or default_setup()
    skipped = []
    checked_folder(volumes.internal)
    packs, texts = gather(volumes, extras, skipped)
    language = extras.language or saved_language(volumes.internal)
    data, result = build(setup, packs, texts, language, options)
    return Prepared(data, result, skipped, started)


def finish(volumes, prepared, dry_run=False):
    """Installs prepared bytes on internal storage and reports what happened."""
    action = install(volumes.internal, prepared.data, dry_run)
    result = prepared.plan
    return Report(extra_path(volumes.internal), action, result.style, len(result.characters),
                  result.unrenderable, result.languages, len(prepared.data or b""),
                  time.monotonic() - prepared.started, prepared.skipped)


def rebuild(volumes, extras=None, options=None, setup=None):
    """Gathers the characters, builds extra.font and installs it on internal storage."""
    options = options or Options()
    return finish(volumes, prepare(volumes, extras, options, setup), options.dry_run)


def coverage_candidates(recipe, options):
    sets = []
    for style in STYLES:
        for weight in WEIGHTS:
            source = recipe["sources"][style][weight]
            font = TTFont(str(font_file(source, options)), fontNumber=source["font_number"], lazy=True)
            sets.append({codepoint for codepoint in font["cmap"].getBestCmap()
                         if codepoint <= MAX_CODEPOINT and unicodedata.category(chr(codepoint)) not in NEVER_DRAWN})
    return sorted(set.intersection(*sets))


def compute_coverage(recipe, options):
    """Ranges every face of every style draws within its bitmap limit: the cmap intersection
    less any character some face drops."""
    candidates = coverage_candidates(recipe, options)
    dropped = set()
    for style in STYLES:
        sources = recipe["sources"][style]
        for spec in recipe["faces"]:
            source = sources[spec["weight"]]
            dropped |= render_face(font_file(source, options), spec, source["font_number"], candidates)[1]
    return ranges_of(set(candidates) - dropped)


def read_texts(arguments):
    texts = [Path(path).read_text(encoding="utf-8") for path in arguments.text]
    for path in arguments.catalogue:
        texts += [value for _, value in lang_pack.read_entries(Path(path))]
    return texts


def run_build(arguments):
    options = Options(arguments.fonts_dir, arguments.dry_run)
    volumes = Volumes(arguments.internal, arguments.music)
    report = rebuild(volumes, Extras(arguments.language), options, default_setup(arguments.recipe))
    print(json.dumps(report.as_dict(), ensure_ascii=False, indent=2))
    return 0


def run_render(arguments):
    started = time.monotonic()
    setup = default_setup(arguments.recipe)
    packs = dict(pack_strings(Path(path).read_bytes()) for path in arguments.pack)
    texts = read_texts(arguments)
    for path in arguments.library:
        texts += library_strings(Path(path).read_bytes())
    data, result = build(setup, packs, texts, arguments.language, Options(arguments.fonts_dir))
    if data is not None:
        arguments.output.write_bytes(data)
    print(json.dumps({"style": result.style, "characters": len(result.characters),
                      "unrenderable": len(result.unrenderable), "languages": result.languages,
                      "size": len(data or b""), "seconds": round(time.monotonic() - started, 3)}))
    return 0


def run_coverage(arguments):
    recipe = load_recipe(arguments.recipe)
    coverage = compute_coverage(recipe, Options(arguments.fonts_dir))
    count = sum(last - first + 1 for first, last in coverage)
    if arguments.check:
        if coverage != recipe["coverage"]:
            print(f"out of date: {arguments.recipe} coverage ({count:,} characters computed)")
            return 1
        return 0
    recipe["coverage"] = coverage
    arguments.recipe.write_text(recipe_text(recipe), encoding="utf-8")
    print(f"{count:,} characters in {len(coverage):,} ranges")
    return 0


def recipe_text(recipe):
    text = json.dumps({key: value for key, value in recipe.items() if key != "coverage"}, indent=2)
    ranges = ",\n    ".join(json.dumps(pair) for pair in recipe["coverage"])
    return text[:-2] + ',\n  "coverage": [\n    ' + ranges + "\n  ]\n}\n"


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Build extra.font, the glyphs the player's fonts lack")
    commands = parser.add_subparsers(dest="command", required=True)
    install_command = commands.add_parser("build", help="rebuild extra.font on a mounted player")
    install_command.add_argument("--internal", type=Path, required=True, help="the player's internal storage")
    install_command.add_argument("--music", type=Path, help="the music card, when it holds the library")
    install_command.add_argument("--language", help="menu language (default: the saved setting)")
    install_command.add_argument("--dry-run", action="store_true")
    render_command = commands.add_parser("render", help="build extra.font into a file")
    render_command.add_argument("--output", type=Path, required=True)
    render_command.add_argument("--language")
    render_command.add_argument("--pack", type=Path, action="append", default=[])
    render_command.add_argument("--library", type=Path, action="append", default=[])
    render_command.add_argument("--text", type=Path, action="append", default=[],
                                help="a UTF-8 text file")
    render_command.add_argument("--catalogue", type=Path, action="append", default=[],
                                help="a translation in STR_NAME = text lines")
    coverage_command = commands.add_parser("coverage", help="compute recipe.json coverage")
    coverage_command.add_argument("--check", action="store_true")
    for command in (install_command, render_command, coverage_command):
        command.add_argument("--recipe", type=Path, default=RECIPE)
        command.add_argument("--fonts-dir", type=Path, help="folder holding the pinned fonts (offline use)")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    runner = {"build": run_build, "render": run_render, "coverage": run_coverage}[arguments.command]
    try:
        return runner(arguments)
    except (RecipeError, lang_pack.PackError, glyph_pack.PackError, OSError) as error:
        print(f"font_recipe: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
