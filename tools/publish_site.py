#!/usr/bin/env python3
"""Publish public static files through the DoublePaaS owner helper.

No firmware, source archives, or deployment configuration is served. Existing
releases remain available for rollback. Publishing does not restart nginx.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
PUBLIC_FILES = (
    "index.html", "firmware.html", "hardware.html", "install.html", "build.html",
    "styles.css", "site.js", "favicon.svg", "robots.txt", "sitemap.xml",
    "languages.html", "language-catalog.js", "language-packs.js",
)
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".svg", ".avif"}


def public_files():
    paths = [SITE / name for name in PUBLIC_FILES]
    assets = SITE / "assets"
    if assets.is_symlink():
        raise SystemExit("site/assets must not be a symlink")
    if assets.exists():
        for path in sorted(assets.rglob("*")):
            if path.is_symlink():
                raise SystemExit(f"Refusing symlink: {path}")
            if path.is_dir():
                continue
            if path.suffix.lower() not in IMAGE_SUFFIXES:
                raise SystemExit(f"Only public images belong under site/assets: {path}")
            paths.append(path)
    files = {}
    total = 0
    for path in paths:
        relative = path.relative_to(SITE)
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f"Expected a regular public file: {path}")
        if any(part.startswith(".") for part in relative.parts):
            raise SystemExit(f"Refusing hidden asset: {path}")
        data = path.read_bytes()
        total += len(data)
        if total > 64 * 1024 * 1024:
            raise SystemExit("Public site exceeds the 64 MiB publication limit")
        files[relative.as_posix()] = data
    return files


def publication_root():
    mounts = []
    for line in (SITE / "deploy/compose.yaml").read_text().splitlines():
        fields = line.strip().removeprefix("- ").split(":")
        if len(fields) == 3 and fields[1:] == ["/site", "ro"]:
            path = Path(fields[0])
            if path.parts[:3] != ("/", "srv", "hosted-data") or len(path.parts) != 5 or path.name != "frontend" or ".." in path.parts:
                raise SystemExit("The public frontend mount must name this site's hosted-data directory")
            mounts.append(str(path))
    if len(mounts) != 1:
        raise SystemExit("Expected one read-only public frontend mount in the deployment contract")
    return mounts[0]


def release_id(files):
    identity = [[name, hashlib.sha256(data).hexdigest()] for name, data in sorted(files.items())]
    return hashlib.sha256(json.dumps(identity, separators=(",", ":")).encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate public files and print the release ID without connecting")
    parser.add_argument("--admin", type=Path, default=Path.home() / "Workspaces/DoublePaaS/scripts/admin.py")
    args = parser.parse_args()
    files = public_files()
    destination = publication_root()
    identity = release_id(files)
    if args.check:
        print(json.dumps({"release": identity, "files": sorted(files), "bytes": sum(map(len, files.values()))}))
        return
    if not Path("/Volumes/Scratch").is_mount():
        parser.error("Mount /Volumes/Scratch before publishing")
    if not args.admin.is_file():
        parser.error("The DoublePaaS owner admin helper is missing")
    payload = {name: base64.b64encode(data).decode("ascii") for name, data in files.items()}
    # Local source is encoded as data, never interpolated into shell commands.
    script = """set -e
python3 - <<'STATIC_SITE_PUBLISH'
import base64, fcntl, hashlib, json, os, shutil, uuid
from pathlib import Path
files = {name: base64.b64decode(data, validate=True) for name, data in PAYLOAD.items()}
identity = RELEASE_ID
root = Path(PUBLICATION_ROOT)
root.mkdir(parents=True, exist_ok=True, mode=0o755)
releases = root / 'releases'
releases.mkdir(exist_ok=True, mode=0o755)
with (root / '.publish.lock').open('a') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    release = releases / identity
    if release.exists():
        if release.is_symlink() or not release.is_dir():
            raise SystemExit('Existing release is not a directory')
        existing = {p.relative_to(release).as_posix(): p for p in release.rglob('*') if not p.is_dir()}
        if set(existing) != set(files) or any(p.is_symlink() or p.read_bytes() != files[name] for name, p in existing.items()):
            raise SystemExit('Existing release differs; preserved without activation')
    else:
        stage = releases / ('.incoming-' + uuid.uuid4().hex)
        stage.mkdir(mode=0o755)
        try:
            for name, data in files.items():
                path = stage / name
                path.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                with path.open('xb') as output:
                    output.write(data)
                    output.flush()
                    os.fsync(output.fileno())
                path.chmod(0o644)
                if path.read_bytes() != data:
                    raise SystemExit('Publication readback differs')
            os.rename(stage, release)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
    current = root / 'current'
    if current.exists() and not current.is_symlink():
        raise SystemExit('Current is not a symlink; preserved without activation')
    previous = os.readlink(current) if current.is_symlink() else None
    temporary = root / ('.current-' + uuid.uuid4().hex)
    try:
        temporary.symlink_to('releases/' + identity)
        os.replace(temporary, current)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps({'release': identity, 'previous': previous, 'files': len(files)}))
STATIC_SITE_PUBLISH
""".replace("PUBLICATION_ROOT", repr(destination), 1).replace("identity = RELEASE_ID", "identity = " + repr(identity), 1).replace("PAYLOAD.items()", repr(payload) + ".items()", 1)
    subprocess.run(["python3", "-B", str(args.admin)], input=script.encode(), check=True)


if __name__ == "__main__":
    main()
