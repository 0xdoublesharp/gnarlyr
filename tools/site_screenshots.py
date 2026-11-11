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
import zlib

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")
DEFAULT_CONFIG = ROOT / "tools/site_screenshots.json"
IDENTITY = json.loads((ROOT / "firmware/identity.json").read_text())
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
        if not isinstance(keys, list) or len(keys) > 100 or any(not isinstance(k, str) or not KEY.fullmatch(k) for k in keys):
            raise ValueError(f"Invalid key sequence for {name}")
        bounded(case.get("frames", defaults.get("frames", 900)), 100, 20000, "frames")
        if not isinstance(case.get("expected_title"), str) or not case["expected_title"].strip():
            raise ValueError(f"Expected screen title is required for {name}")
    return config


def png_bytes(width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    stride = width * 3
    rows = b"".join(b"\0" + rgb[row:row + stride] for row in range(0, len(rgb), stride))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


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


def create_demo(config, work, env):
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
        command = ["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo", "-t", str(duration), "-c:a", "flac"]
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
    python = ROOT / "firmware/.venv/bin/python"
    run([python, ROOT / "firmware/tools/library_index.py", "--music", card, "--output", library], work / "index.log", env)
    return card, library


def render_case(case, context):
    work, env = context["work"], context["env"]
    system = work / (case["name"] + "-system")
    system.mkdir()
    shutil.copyfile(context["library"], system / "library.db")
    bmp = work / (case["name"] + ".bmp")
    command = [context["simulator"], "--headless", "--frames", str(case.get("frames", context["defaults"].get("frames", 900))),
               "--battery", str(context["defaults"].get("battery", 80)), "--card", context["card"], "--system-dir", system, "--screenshot", bmp]
    if case.get("keys"):
        command += ["--keys", ",".join(case["keys"])]
    result = run(command, work / (case["name"] + ".log"), env)
    if not any(line.startswith(case["expected_title"] + ":") for line in result.splitlines()):
        raise ValueError(f"{case['name']} did not reach {case['expected_title']!r}; inspect {work / (case['name'] + '.log')}")
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
    parser.add_argument("--simulator", type=Path, default=ROOT / "firmware/build/dev/platform/sim/echo_sim")
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
        parent = SCRATCH / "tmp/site-screenshots"
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        env = {**os.environ, "TMPDIR": str(work), "PYTHONDONTWRITEBYTECODE": "1"}
        if not args.no_build:
            run([sys.executable, ROOT / "tools/firmware.py", "build", "--preset", "dev", "--target", "echo_sim", "--jobs", "4"], work / "build.log", env)
        if not args.simulator.is_file():
            raise ValueError("Build the simulator first, or pass --simulator")
        card, library = create_demo(config, work, env)
        context = {"work": work, "env": env, "simulator": args.simulator.resolve(), "card": card, "library": library, "defaults": config.get("defaults", {})}
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
