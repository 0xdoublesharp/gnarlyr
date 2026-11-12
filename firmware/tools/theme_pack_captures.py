import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import wave

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "firmware/tools"))
from library_index import TrackInfo, build_library, encode_library

SPEC = importlib.util.spec_from_file_location("theme_pack", ROOT / "tools/theme_pack.py")
theme = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(theme)


def music(card):
    card.mkdir(parents=True, exist_ok=True)
    tracks = []
    for index, title in enumerate(("Night Shift", "Green Phosphor", "Open Window"), 1):
        name = f"{index:02} {title}.wav"
        with wave.open(str(card / name), "wb") as output:
            output.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
            pcm = b"".join(struct.pack("<h", int(1000 * math.sin(at * math.tau * (160 + index * 40) / 16000)))
                           for at in range(16000 * 30))
            output.writeframes(pcm)
        tracks.append(TrackInfo(path=name, title=title, artist="Pixel Workshop", album_artist="Pixel Workshop",
                                album="After Hours", genre="Electronic", year=2026, disc=1,
                                number=index, duration_ms=30000))
    return encode_library(build_library(tracks))


def run_sim(args, card, state, keys, output, frames=280):
    command = [str(args.sim), "--headless", "--frames", str(frames), "--card", str(card),
               "--system-dir", str(state), "--clock", "1794447000"]
    if keys:
        command += ["--keys", keys]
    if output:
        command += ["--screenshot", str(output)]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return result.stdout.strip()


def state_for(args, state, pack, library, size, orientation):
    if state.exists():
        shutil.rmtree(state)
    state.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.state_tool), "--state", str(pack), str(state), str(size), str(orientation)],
                   check=True, capture_output=True)
    (state / "library.db").write_bytes(library)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", type=Path, required=True)
    parser.add_argument("--state-tool", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(Path("/Volumes/Scratch/SnowskyEcho")):
        parser.error("captures and state must stay on Scratch")
    output.mkdir(parents=True, exist_ok=True)
    card = output / "card"
    library = music(card)
    identity = json.loads((ROOT / "firmware/identity.json").read_text()) if (ROOT / "firmware/identity.json").exists() else json.loads((ROOT / "identity.json").read_text())
    themes = card / identity["data_directory"] / "themes"
    themes.mkdir(parents=True, exist_ok=True)
    packs = {}
    for source in sorted((ROOT / "examples/themes").iterdir()):
        if not source.is_dir():
            continue
        data = theme.build(source)
        info = theme.check(data)
        path = themes / (info["id"] + ".gtheme")
        path.write_bytes(data)
        packs[source.name] = path
    views = {"home": "", "list": "play", "now-playing": ",".join(["play"] + ["down"] * 7 + ["play", "play"]),
             "settings": "down,play", "theme-picker": ",".join(["down", "play"] + ["down"] * 22 + ["play"])}
    captures = []
    for name, pack in packs.items():
        for view, keys in views.items():
            for orientation in range(2):
                for size in range(3):
                    stem = f"{name}-{view}-{'portrait' if orientation else 'landscape'}-{('small','medium','large')[size]}"
                    state = output / "state" / stem
                    state_for(args, state, pack, library, size, orientation)
                    bmp = output / (stem + ".bmp")
                    mapped = ",".join({"up": "down", "down": "up"}.get(key, key)
                                      for key in keys.split(",")) if orientation == 1 else keys
                    log = run_sim(args, card, state, mapped, bmp, max(280, 110 + len(keys.split(',')) * 10))
                    expected = {"home": identity["short_name"], "list": "Music", "now-playing": "Now Playing",
                                "settings": "Settings", "theme-picker": "Theme"}[view]
                    if not log.startswith(expected + ":"):
                        raise ValueError(f"{stem}: expected {expected}, saw {log}")
                    png = output / (stem + ".png")
                    with Image.open(bmp) as image:
                        image.save(png)
                    bmp.unlink()
                    captures.append((stem, png, log))
                    print(stem + ": " + log, flush=True)
    cell_width, cell_height = 300, 330
    sheet = Image.new("RGB", (cell_width * 6, cell_height * (len(captures) // 6)), "#15171b")
    draw = ImageDraw.Draw(sheet)
    for index, (stem, png, _) in enumerate(captures):
        column, row = index % 6, index // 6
        with Image.open(png) as image:
            image.thumbnail((288, 288))
            x = column * cell_width + (cell_width - image.width) // 2
            y = row * cell_height + 34
            sheet.paste(image, (x, y))
        label = stem.replace("landscape", "L").replace("portrait", "P")
        draw.text((column * cell_width + 6, row * cell_height + 5), label, fill="white")
    sheet.save(output / "contact-sheet.png")
    measurements = []
    pack = next(iter(packs.values()))
    for mode in ("builtin", *packs):
        for view in ("home", "list"):
            for repeat in range(3):
                state = output / "state" / f"bench-{mode}-{view}-{repeat}"
                state_for(args, state, packs.get(mode, pack), library, 1, 0)
                if mode == "builtin":
                    (state / "theme.bin").unlink()
                keys = (["play"] if view == "list" else []) + ["down", "up"] * 14
                log = run_sim(args, card, state, ",".join(keys), None, 440)
                match = re.search(r"frame ([0-9.]+) ms avg, ([0-9.]+) ms worst", log)
                if match is None:
                    raise ValueError(log)
                measurements.append({"mode": mode, "view": view, "repeat": repeat,
                                     "average_ms": float(match[1]), "worst_ms": float(match[2]), "simulator": log})
    (output / "frame-times.json").write_text(json.dumps(measurements, indent=2) + "\n")
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    main()
