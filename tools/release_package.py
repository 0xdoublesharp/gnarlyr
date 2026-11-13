#!/usr/bin/env python3
"""Turn a release tag into its build label, package checked assets and verify them.

A tag is v<identity version> for a release or v<identity version>-<label> for a
pre-release such as v1.0.0-beta.1. The label is written into the image's 8-byte
build field, so Settings → About shows 1.0.0-beta.1 and debug reports name the
label as the build. Without a label the build field keeps the short commit.
A pre-release ships the device preset, which writes debug reports for testers;
a release ships the release preset.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = json.loads((ROOT / "firmware/identity.json").read_text(encoding="utf-8"))
IMAGE = IDENTITY["update_file"]
SOURCE = "source.tar.gz"
MANIFEST = "firmware.json"
SUMS = "SHA256SUMS"
STOCK_IMAGE = "ECHOECHO.IMG"
MODEL = "SNOWSKY ECHO (full-size)"
NOTES = ROOT / "firmware/docs/release-notes.md"
TAG = re.compile(r"v(\d+\.\d+\.\d+)(?:-([0-9A-Za-z][0-9A-Za-z.]{0,7}))?")
FIELD = 8
BUILD_FIELD = slice(0x30, 0x30 + FIELD)


def source_name():
    return IDENTITY["short_name"].lower() + "-" + SOURCE


def label(tag, version=IDENTITY["version"]):
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError(f"{tag}: use v<major.minor.patch> or v<major.minor.patch>-<label of up to {FIELD} characters>")
    if match[1] != version:
        raise ValueError(f"{tag}: firmware/identity.json has version {version}")
    return match[2] or ""


def release_name(build_label, version=IDENTITY["version"]):
    return version + ("-" + build_label if build_label else "")


def preset(build_label):
    return "device" if build_label else "release"


def image_build(data):
    return data[BUILD_FIELD].rstrip(b"\0").decode("ascii")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(image, source, dist, commit, tag=None):
    build_label = label(tag) if tag else ""
    dist.mkdir(parents=True, exist_ok=True)
    data = image.read_bytes()
    if build_label and image_build(data) != build_label:
        raise ValueError(f"{image}: its build field is {image_build(data)!r}, not {build_label!r}; build with ECHO_BUILD_TAG={build_label}")
    names = [IMAGE, STOCK_IMAGE, source_name()]
    shutil.copyfile(image, dist / IMAGE)
    shutil.copyfile(image, dist / STOCK_IMAGE)
    shutil.copyfile(source, dist / source_name())
    files = {name: {"sha256": digest(dist / name), "bytes": (dist / name).stat().st_size} for name in names}
    manifest = {"version": IDENTITY["version"], "release": release_name(build_label), "build": image_build(data),
                "preset": preset(build_label), "commit": commit, "model": MODEL, "files": files}
    (dist / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n")
    names.append(MANIFEST)
    (dist / SUMS).write_text("".join(f"{digest(dist / name)}  {name}\n" for name in names))
    return manifest


def check_sums(dist):
    listed = {}
    for line in (dist / SUMS).read_text().splitlines():
        value, name = line.split("  ", 1)
        if digest(dist / name) != value:
            raise ValueError(f"{name}: SHA-256 does not match {SUMS}")
        listed[name] = value
    return listed


def verify(dist, tag, commit):
    build_label = label(tag)
    listed = check_sums(dist)
    manifest = json.loads((dist / MANIFEST).read_text())
    problems = []
    if manifest["preset"] != preset(build_label):
        problems.append(f"the manifest names the {manifest['preset']} preset, not {preset(build_label)}")
    if manifest["commit"] != commit:
        problems.append(f"the manifest names commit {manifest['commit']}, not {commit}")
    if "v" + manifest["release"] != tag:
        problems.append(f"the manifest names release {manifest['release']}, not {tag[1:]}")
    for name, entry in manifest["files"].items():
        data = (dist / name).read_bytes()
        if entry != {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)} or name not in listed:
            problems.append(f"{name} differs from the manifest or is missing from {SUMS}")
    image = (dist / IMAGE).read_bytes()
    if manifest["build"] != image_build(image) or (build_label and build_label != image_build(image)):
        problems.append(f"{IMAGE} has build {image_build(image)!r}; the manifest says {manifest['build']!r} and the tag {build_label or 'a commit'!r}")
    if set(manifest["files"]) != {IMAGE, STOCK_IMAGE, source_name()}:
        problems.append(f"the manifest lists {sorted(manifest['files'])}")
    elif (dist / STOCK_IMAGE).read_bytes() != image:
        problems.append(f"{STOCK_IMAGE} is not a copy of {IMAGE}")
    if problems:
        raise ValueError("; ".join(problems))
    return manifest


def notes(tag, path=NOTES):
    release = release_name(label(tag))
    lines, body = path.read_text().splitlines(), None
    for line in lines:
        if line.startswith("## "):
            if body is not None:
                break
            if line[3:].split()[:1] == [release]:
                body = []
                continue
        if body is not None:
            body.append(line)
    if body is None:
        raise ValueError(f"{path.name} has no section headed {release}")
    return "\n".join(body).strip() + "\n"


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("label", help="print the build label for a tag; empty for a release")
    command.add_argument("tag")
    command = commands.add_parser("preset", help="print the image preset a tag ships")
    command.add_argument("tag")
    command = commands.add_parser("package", help="copy the image, its copy for FiiO's updater and the source, then write the manifest and sums")
    command.add_argument("--image", type=Path, required=True)
    command.add_argument("--source", type=Path, required=True)
    command.add_argument("--dist", type=Path, required=True)
    command.add_argument("--commit", required=True)
    command.add_argument("--tag")
    command = commands.add_parser("verify", help="check a packaged release against its tag and commit")
    command.add_argument("--dist", type=Path, required=True)
    command.add_argument("--tag", required=True)
    command.add_argument("--commit", required=True)
    command = commands.add_parser("notes", help="print the release-notes section for a tag")
    command.add_argument("tag")
    return parser.parse_args(argv)


def main(argv=None):
    args = arguments(argv)
    try:
        if args.command == "label":
            print(label(args.tag))
        elif args.command == "preset":
            print(preset(label(args.tag)))
        elif args.command == "package":
            print(json.dumps(package(args.image, args.source, args.dist, args.commit, args.tag), indent=2))
        elif args.command == "verify":
            verify(args.dist, args.tag, args.commit)
            print(f"{args.dist}: matches {args.tag} at {args.commit}")
        else:
            sys.stdout.write(notes(args.tag))
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
