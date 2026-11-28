#!/usr/bin/env python3
"""Publish public static files through the DoublePaaS owner helper.

No firmware, source archives, or deployment configuration is served. Existing
releases remain available for rollback. Publishing does not restart nginx.
"""
import argparse
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")
PAGEFIND_VERSION = "1.6.0"
SEARCH_HEAD = (b'<link rel="stylesheet" href="/pagefind/pagefind-component-ui.css">\n'
               b'<script type="module" src="/pagefind/pagefind-component-ui.js"></script>\n')
SEARCH_TRIGGER = b'<pagefind-modal-trigger class="site-search"></pagefind-modal-trigger>\n'
SEARCH_MODAL = (b'<pagefind-config bundle-path="/pagefind/"></pagefind-config>\n'
                b'<pagefind-modal></pagefind-modal>\n')
PUBLIC_FILES = (
    "index.html", "companion.html", "firmware.html", "docs.html", "hardware.html", "recovery.html", "install.html", "build.html",
    "styles.css", "site.js", "analytics.js", "favicon.svg", "robots.txt", "sitemap.xml",
    "themes.html", "languages.html", "language-catalog.js", "language-packs.js",
    "comparison.html", "comparison.js", "studio.html", "themes-catalog.json",
    "studio/art-codec.js", "studio/asset-io.js", "studio/audio.js", "studio/builtins.js", "studio/catalog.js",
    "studio/echo-font.js", "studio/echo-renderer.js", "studio/gallery.js", "studio/gtheme-codec.js",
    "studio/palette.js", "studio/png.js", "studio/studio.css", "studio/studio.js", "studio/theme-link.js",
)
STUDIO_REDIRECT = "studio/index.html"
STUDIO_EXAMPLES = ("pocket-scope", "desktop-98", "vice-86")
STUDIO_FONT_FACES = ("inter_regular_16_latin1", *(f"inter_{weight}_{size}"
                     for size in (18, 22, 26) for weight in ("regular", "semibold")))
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".svg", ".avif", ".gif"}


def studio_resources():
    for example in STUDIO_EXAMPLES:
        folder = ROOT / "examples/themes" / example
        manifest_path = folder / "theme.json"
        manifest = json.loads(manifest_path.read_text())
        yield f"studio/examples/{example}/theme.json", manifest_path
        for name in sorted({entry["file"] for entry in manifest["assets"]}):
            if not re.fullmatch(r"[a-zA-Z0-9_-]+\.png", name):
                raise SystemExit(f"Invalid public theme artwork filename: {name}")
            yield f"studio/examples/{example}/{name}", folder / name
    fonts = ROOT / "firmware/src/fonts/packs"
    for face in STUDIO_FONT_FACES:
        yield f"studio/fonts/{face}.bin", fonts / f"{face}.bin"
    yield "studio/fonts/OFL.txt", ROOT / "firmware/assets/fonts/inter/OFL.txt"


def studio_base_font():
    # The on-disk 16px pack extends the glyphs compiled into firmware.
    source = (ROOT / "firmware/src/fonts/fonts.c").read_text()
    bitmap = source.split("INTER_REGULAR_16_BITMAP[] = {", 1)[1].split("};", 1)[0]
    glyphs = source.split("INTER_REGULAR_16_GLYPHS[] = {", 1)[1].split("};", 1)[0]
    atlas = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", bitmap))
    entries = [tuple(map(int, line.split(","))) for line in re.findall(r"\{([-0-9, ]+)\}", glyphs)]
    start = 8 + len(entries) * 12
    if not entries or any(offset + (w * h + 1) // 2 > len(atlas) for _, offset, w, h, _, _, _ in entries):
        raise SystemExit("Invalid firmware 16px base font")
    index = b"".join(struct.pack("<HBBbbBBI", code, w, h, left, top, advance, 0, start + offset)
                     for code, offset, w, h, left, top, advance in entries)
    return struct.pack("<4sI", b"GF16", len(entries)) + index + atlas


def add_search(files):
    scratch_tmp = SCRATCH / "tmp"
    scratch_tmp.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="site-search-", dir=scratch_tmp) as folder:
        root = Path(folder)
        # The guides are currently in English. Every homepage searches the same corpus.
        for name in PUBLIC_FILES:
            if name.endswith(".html"):
                (root / name).write_bytes(files[name])
        env = dict(os.environ, TMPDIR=folder)
        env.setdefault("npm_config_cache", str(SCRATCH / "tools/pagefind-npm"))
        try:
            subprocess.run(["npx", "--yes", f"pagefind@{PAGEFIND_VERSION}", "--site", folder,
                            "--root-selector", "main", "--quiet"],
                           env=env, cwd=root, stdout=subprocess.PIPE, check=True)
        except FileNotFoundError as error:
            raise SystemExit("Install Node.js with npx to build the website search index") from error
        except subprocess.CalledProcessError as error:
            raise SystemExit(f"Pagefind failed to build the website search index (exit {error.returncode})") from error
        bundle = {path.relative_to(root).as_posix(): path.read_bytes()
                  for path in (root / "pagefind").rglob("*") if path.is_file()}
    required = {"pagefind/pagefind-entry.json", "pagefind/pagefind-component-ui.css",
                "pagefind/pagefind-component-ui.js"}
    if not required <= bundle.keys():
        raise SystemExit("Pagefind did not produce a complete search bundle")
    files.update(bundle)
    for name, page in files.items():
        if name.endswith(".html") and name != STUDIO_REDIRECT:
            files[name] = (page.replace(b"</head>", SEARCH_HEAD + b"</head>", 1)
                           .replace(b"<nav aria-label=", SEARCH_TRIGGER + b"<nav aria-label=", 1)
                           .replace(b"</body>", SEARCH_MODAL + b"</body>", 1))


def public_files(*, search=False):
    paths = [(name, SITE / name) for name in PUBLIC_FILES]
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
            relative = path.relative_to(SITE).as_posix()
            paths.append((relative, path))
    files = {}
    total = 0
    for relative, path in (*paths, *studio_resources()):
        parts = Path(relative).parts
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f"Expected a regular public file: {path}")
        if any(part.startswith(".") for part in parts):
            raise SystemExit(f"Refusing hidden asset: {path}")
        data = path.read_bytes()
        total += len(data)
        if total > 64 * 1024 * 1024:
            raise SystemExit("Public site exceeds the 64 MiB publication limit")
        files[relative] = data
    files["studio/fonts/inter_regular_16_core.bin"] = studio_base_font()
    if (SITE / "i18n/locales.json").is_file():
        spec = importlib.util.spec_from_file_location("site_locales", Path(__file__).with_name("site_locales.py"))
        locales = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(locales)
        files = locales.build(files, SITE)
    redirect = SITE / STUDIO_REDIRECT
    if redirect.is_symlink() or not redirect.is_file():
        raise SystemExit(f"Expected a regular Studio redirect file: {redirect}")
    files[STUDIO_REDIRECT] = redirect.read_bytes()
    if search:
        if not SCRATCH.parent.is_mount():
            raise SystemExit("Mount /Volumes/Scratch before building website search")
        add_search(files)
    assets = {name.encode(): name.encode() + b"?v=" + hashlib.sha256(data).hexdigest()[:12].encode()
              for name, data in files.items() if not name.endswith(".html")}
    def version_link(match):
        target, separator, fragment = match[2].partition(b"#")
        path = target.split(b"?", 1)[0]
        absolute = path.startswith(b"/") and not path.startswith(b"//")
        key = path[1:] if absolute else path
        if key not in assets:
            return match[0]
        return match[1] + (b"/" if absolute else b"") + assets[key] + separator + fragment + match[3]
    for name, data in files.items():
        if name.endswith(".html"):
            files[name] = re.sub(rb"""((?:href|src)=["'])([^"']+)(["'])""", version_link, data)
    if sum(map(len, files.values())) > 64 * 1024 * 1024:
        raise SystemExit("Public site exceeds the 64 MiB publication limit")
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
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="Validate public files and print the release ID without publishing")
    mode.add_argument("--preview", type=Path, metavar="DIRECTORY", help="Write the complete site to a new Scratch directory")
    parser.add_argument("--admin", type=Path, default=Path.home() / "Workspaces/DoublePaaS/scripts/admin.py")
    args = parser.parse_args()
    preview = args.preview.resolve() if args.preview else None
    if preview is not None and (not preview.is_relative_to(SCRATCH) or preview.exists()):
        parser.error("Preview output must be a new directory under /Volumes/Scratch/SnowskyEcho")
    files = public_files(search=True)
    if preview is not None:
        preview.mkdir(parents=True)
        for name, data in files.items():
            target = preview / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        print(json.dumps({"output": str(preview), "release": release_id(files)}))
        return
    destination = publication_root()
    identity = release_id(files)
    if args.check:
        print(json.dumps({"release": identity, "files": sorted(files), "bytes": sum(map(len, files.values()))}))
        return
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
