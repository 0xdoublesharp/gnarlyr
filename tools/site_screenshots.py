"""Render the website's firmware screenshots without connecting to a player."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from echo_debug import IDENTITY, png_bytes

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")
DEFAULT_CONFIG = ROOT / "tools/site_screenshots.json"
KEY = re.compile(r"(?:up|down|play|menu|back|power)(?::(?:long|held))?\Z")
NAME = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*\Z")


def bounded(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be an integer from {minimum} to {maximum}")
    return value


def load_config(path):
    text = Path(path).read_text().replace("{firmware_name}", json.dumps(IDENTITY["name"])[1:-1])
    config = json.loads(text)
    if config.get("version") != 1 or not isinstance(config.get("screens"), list) or not config["screens"]:
        raise ValueError("Expected version 1 and a nonempty screens list")
    defaults = config.get("defaults", {})
    bounded(defaults.get("frames", 900), 100, 20000, "frames")
    bounded(defaults.get("battery", 80), 0, 100, "battery")
    if config.get("demo", {}).get("audio_source", "silence") not in ("silence", "tone"):
        raise ValueError("Demo audio_source must be silence or tone")
    names, files = set(), set()
    for case in config["screens"]:
        name, filename = case.get("name", ""), case.get("file", "")
        if not isinstance(name, str) or not NAME.fullmatch(name) or filename != name + ".png":
            raise ValueError("Each screen needs a lowercase hyphenated name and matching .png filename")
        if name in names or filename in files:
            raise ValueError("Screen names and output filenames must be unique")
        names.add(name)
        files.add(filename)
        keys = case.get("keys", [])
        if not isinstance(keys, list) or len(keys) > 64 or any(not isinstance(k, str) or not KEY.fullmatch(k) for k in keys):
            raise ValueError(f"Invalid key sequence for {name}")
        native = case.get("native_snapshot")
        if native is not None and (not isinstance(native, str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*-[0-9]+\.ppm", native)):
            raise ValueError("Native snapshots must name one generated PPM file")
        theme = case.get("theme_example")
        if theme is not None and (not isinstance(theme, str) or not NAME.fullmatch(theme)):
            raise ValueError("Theme examples must name a source example folder")
        language = case.get("language")
        if language is not None:
            if not isinstance(language, dict) or not isinstance(language.get("code"), str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9-]{0,4}", language["code"]):
                raise ValueError(f"Invalid language code for {name}")
            strings = language.get("strings")
            if not isinstance(strings, dict) or not strings or any(
                    not isinstance(key, str) or not re.fullmatch(r"STR_[A-Z0-9_]+", key) or
                    not isinstance(value, str) or not value.strip() or any(c in value for c in "\n\r\0")
                    for key, value in strings.items()):
                raise ValueError(f"Invalid language strings for {name}")
        bounded(case.get("frames", defaults.get("frames", 900)), 100, 20000, "frames")
        if not isinstance(case.get("expected_title"), str) or not case["expected_title"].strip():
            raise ValueError(f"Expected screen title is required for {name}")
    return config


def ppm_to_png(data):
    header = re.match(rb"P6\n([0-9]+) ([0-9]+)\n255\n", data)
    if header is None:
        raise ValueError("Expected an RGB PPM capture")
    width, height = map(int, header.groups())
    rgb = data[header.end():]
    if not 1 <= width <= 1600 or not 1 <= height <= 1600 or len(rgb) != width * height * 3:
        raise ValueError("Native screenshot dimensions or pixel data are invalid")
    return png_bytes(width, height, rgb)


def bmp_to_png(data):
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Screenshot is not a BMP")
    offset, = struct.unpack_from("<I", data, 10)
    header, width, height, planes, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
    if header < 40 or offset < 14 + header or planes != 1 or bits not in (24, 32) or compression != 0:
        raise ValueError("Expected uncompressed 24-bit or 32-bit BMP")
    if not 1 <= width <= 1600 or not 1 <= abs(height) <= 1600:
        raise ValueError("Screenshot dimensions are outside the supported range")
    stride = ((width * bits + 31) // 32) * 4
    if offset + stride * abs(height) > len(data):
        raise ValueError("Screenshot pixel data is truncated")
    rgb = bytearray()
    for row in range(abs(height)):
        source_row = abs(height) - row - 1 if height > 0 else row
        for column in range(width):
            at = offset + source_row * stride + column * (bits // 8)
            rgb.extend((data[at + 2], data[at + 1], data[at]))
    return png_bytes(width, abs(height), bytes(rgb))


def run(command, log, env):
    result = subprocess.run([str(part) for part in command], env=env, cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
    log.write_text(result.stdout)
    if result.returncode:
        raise ValueError(f"Command failed; see {log}")
    return result.stdout


def create_demo(config, work, env, source_root=ROOT):
    card = work / "music"
    card.mkdir()
    demo = config.get("demo", {})
    tracks = demo.get("tracks", [])
    if not 1 <= len(tracks) <= 32:
        raise ValueError("Demo needs 1–32 tracks")
    for index, track in enumerate(tracks):
        folder = card / f"Album {track.get('album', 'Demo')}"
        if folder.parent != card or '/' in str(track.get('album', 'Demo')) or '\\' in str(track.get('album', 'Demo')):
            raise ValueError("Demo album names cannot contain path separators")
        folder.mkdir(exist_ok=True)
        audio = folder / f"{index + 1:02d}.flac"
        duration = bounded(track.get("seconds", 30), 5, 120, "demo duration")
        signal = "anullsrc=r=44100:cl=stereo"
        if demo.get("audio_source") == "tone":
            signal = "aevalsrc=0.18*sin(2*PI*220*t)+0.08*sin(2*PI*880*t)|0.16*sin(2*PI*330*t)+0.07*sin(2*PI*1320*t):s=44100"
        command = ["ffmpeg", "-v", "error", "-f", "lavfi", "-i", signal, "-t", str(duration), "-c:a", "flac"]
        for field in ("title", "artist", "album"):
            command += ["-metadata", field + "=" + track.get(field, "Demo")]
        run(command + [audio], work / f"track-{index}.log", env)
        audio.with_suffix(".lrc").write_text(demo.get("lyrics", "[00:00.00]A new day begins\n[00:03.00]Take the long way home\n"))
    book = demo.get("audiobook")
    if book:
        folder = card / "Audiobooks"
        folder.mkdir()
        metadata = work / "chapters.txt"
        title, artist = book.get("title", "City Stories"), book.get("artist", IDENTITY["name"] + " Demo")
        if any(not isinstance(value, str) or any(c in value for c in "\n\r;#=\\") for value in (title, artist)):
            raise ValueError("Book title and artist must be plain single-line text")
        parts = [";FFMETADATA1", "title=" + title, "artist=" + artist]
        for index, title in enumerate(book.get("chapters", ["Opening", "The Middle", "Finale"])):
            if not isinstance(title, str) or any(c in title for c in "\n\r;#=\\"):
                raise ValueError("Chapter titles must be plain single-line text")
            parts += ["[CHAPTER]", "TIMEBASE=1/1000", f"START={index * 10000}", f"END={(index + 1) * 10000}", "title=" + title]
        metadata.write_text("\n".join(parts) + "\n")
        count = len(book.get("chapters", ["Opening", "The Middle", "Finale"]))
        bounded(count, 1, 12, "chapter count")
        run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo", "-f", "ffmetadata", "-i", metadata,
             "-t", str(count * 10), "-map_metadata", "1", "-map_chapters", "1", "-c:a", "aac", "-b:a", "32k", folder / "City Stories.m4b"], work / "book.log", env)
    library = work / "library.db"
    python = source_root / "firmware/.venv/bin/python"
    run([python, source_root / "firmware/tools/library_index.py", "--music", card, "--output", library], work / "index.log", env)
    return card, library


def prepare_language(case, system, work, env, source_root=ROOT):
    language = case.get("language")
    if language is None:
        return
    source = work / (case["name"] + "-language")
    source.mkdir()
    shutil.copyfile(source_root / "firmware/assets/lang/en.txt", source / "en.txt")
    code = language["code"]
    (source / (code + ".txt")).write_text("\n".join(
        key + " = " + value for key, value in language["strings"].items()) + "\n", encoding="utf-8")
    output = system / "lang"
    output.mkdir()
    run([source_root / "firmware/.venv/bin/python", source_root / "firmware/tools/lang_pack.py",
         "--languages", source, "--pack", code, "--output", output / (code + ".pack")],
        work / (case["name"] + "-language.log"), env)


def render_case(case, context):
    work, env = context["work"], context["env"]
    if case.get("native_snapshot"):
        data = ppm_to_png((context["native"] / case["native_snapshot"]).read_bytes())
        if list(struct.unpack_from(">II", data, 16)) != case.get("size", [480, 222]):
            raise ValueError("Native screenshot dimensions do not match the scene")
        return data
    system = work / (case["name"] + "-system")
    system.mkdir()
    shutil.copyfile(context["library"], system / "library.db")
    source_root = context.get("source_root", ROOT)
    prepare_language(case, system, work, env, source_root)
    card = context["card"]
    if case.get("theme_example"):
        examples = (source_root / "examples/themes").resolve()
        example = (examples / case["theme_example"]).resolve()
        if not example.is_relative_to(examples) or not (example / "theme.json").is_file():
            raise ValueError("Theme source example is unavailable")
        card = work / (case["name"] + "-card")
        shutil.copytree(context["card"], card)
        packs = card / IDENTITY["data_directory"] / "themes"
        packs.mkdir(parents=True, exist_ok=True)
        run([source_root / "firmware/.venv/bin/python", source_root / "tools/theme_pack.py",
             example, "--output", packs / (case["theme_example"] + ".gtheme")],
            work / (case["name"] + "-theme.log"), env)
    bmp = work / (case["name"] + ".bmp")
    command = [context["simulator"], "--headless", "--frames", str(case.get("frames", context["defaults"].get("frames", 900))),
               "--battery", str(context["defaults"].get("battery", 80)), "--card", card, "--system-dir", system, "--screenshot", bmp]
    if case.get("keys"):
        command += ["--keys", ",".join(case["keys"])]
    result = run(command, work / (case["name"] + ".log"), env)
    if not any(line.startswith(case["expected_title"] + ":") for line in result.splitlines()):
        raise ValueError(f"{case['name']} did not reach {case['expected_title']!r}; inspect {work / (case['name'] + '.log')}")
    if case.get("language"):
        schema = (source_root / "firmware/src/settings/settings.c").read_text()
        offset = int(re.search(r"\bLANGUAGE_FIELD\s*=\s*(\d+)", schema)[1])
        saved = (system / "settings.bin").read_bytes()
        code = case["language"]["code"].encode("ascii")
        if saved[offset:offset + len(code) + 1] != code + b"\0":
            raise ValueError(f"{case['name']} did not select its configured language")
    data = bmp_to_png(bmp.read_bytes())
    size = struct.unpack_from(">II", data, 16)
    if list(size) != case.get("size", [480, 222]):
        raise ValueError(f"Unexpected dimensions for {case['name']}: {size}")
    return data


def publish_screens(rendered, output):
    output = output.resolve()
    allowed = (ROOT / "site/assets", SCRATCH)
    if not any(output.is_relative_to(path.resolve()) for path in allowed):
        raise ValueError("Screenshot output must be under site/assets or this project's Scratch folder")
    output.mkdir(parents=True, exist_ok=True)
    for filename in rendered:
        if (output / filename).is_symlink():
            raise ValueError(f"Refusing symlink output: {filename}")
    for filename, data in rendered.items():
        with tempfile.NamedTemporaryFile(prefix=".screen-", dir=output, delete=False) as temporary:
            temporary.write(data)
            staged = Path(temporary.name)
        try:
            staged.replace(output / filename)
        finally:
            staged.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--only", action="append", default=[], help="Screen name to render; repeat to select more")
    parser.add_argument("--output", type=Path, default=ROOT / "site/assets/screens")
    parser.add_argument("--source-root", type=Path, default=ROOT, help="Checkout containing the firmware to build and capture")
    parser.add_argument("--simulator", type=Path, help="Override the selected checkout's simulator binary")
    parser.add_argument("--no-build", action="store_true", help="Use an already-built simulator")
    parser.add_argument("--list", action="store_true", help="Validate and list configured screens without building")
    parser.add_argument("--keep-work", action="store_true", help="Keep temporary render files on Scratch")
    args = parser.parse_args()
    work = None
    try:
        config = load_config(args.config)
        screens = [case for case in config["screens"] if not args.only or case["name"] in args.only]
        missing = set(args.only) - {case["name"] for case in screens}
        if missing:
            raise ValueError("Unknown screen names: " + ", ".join(sorted(missing)))
        if args.list:
            print(json.dumps({"screens": screens}))
            return 0
        if not Path("/Volumes/Scratch").is_mount():
            raise ValueError("Mount /Volumes/Scratch before rendering")
        source_root = args.source_root.resolve()
        if not (source_root / "tools/firmware.py").is_file() or not (source_root / "firmware/identity.json").is_file():
            raise ValueError("Source root must contain tools/firmware.py and firmware/identity.json")
        simulator = args.simulator or source_root / "firmware/build/dev/platform/sim/echo_sim"
        parent = SCRATCH / "tmp/site-screenshots"
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        env = {**os.environ, "TMPDIR": str(work), "PYTHONDONTWRITEBYTECODE": "1"}
        if not args.no_build:
            run([sys.executable, source_root / "tools/firmware.py", "build", "--preset", "dev", "--target", "echo_sim", "--jobs", "4"], work / "build.log", env)
        native = work / "native-screens"
        if any(case.get("native_snapshot") for case in screens):
            if not args.no_build:
                run([sys.executable, source_root / "tools/firmware.py", "build", "--preset", "dev", "--target", "test_usb_screen", "--jobs", "4"], work / "native-build.log", env)
            native.mkdir()
            run([source_root / "firmware/build/dev/tests/test_usb_screen"], work / "native-render.log",
                {**env, "ECHO_TEST_SCREENSHOTS": str(native)})
        if not simulator.is_file():
            raise ValueError("Build the simulator first, or pass --simulator")
        card, library = create_demo(config, work, env, source_root)
        context = {"work": work, "env": env, "source_root": source_root, "simulator": simulator.resolve(), "card": card, "library": library, "defaults": config.get("defaults", {}), "native": native}
        rendered = {case["file"]: render_case(case, context) for case in screens}
        publish_screens(rendered, args.output)
        print(json.dumps({"output": str(args.output.resolve()), "screens": [{"file": name, "sha256": hashlib.sha256(data).hexdigest()} for name, data in rendered.items()], "work": str(work) if args.keep_work else None}))
        if not args.keep_work:
            shutil.rmtree(work)
        return 0
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"error": str(error), "work": str(work) if work else None}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
