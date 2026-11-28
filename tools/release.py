#!/usr/bin/env python3
"""Prepare, stage, smoke-test, or promote a firmware release and optionally publish its site."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import firmware
import publish_site
import release_package as package


ROOT = firmware.ROOT
PROJECT = package.IDENTITY["short_name"].lower()
REPO = f"0x{package.IDENTITY['manufacturer'].lower()}/{PROJECT}"
RELEASES = f"https://github.com/{REPO}/releases"
API = f"repos/{REPO}/releases"
THEMES = tuple(f"{name}.gtheme" for name in publish_site.STUDIO_EXAMPLES)
PRESETS = ("device", "release")
PYTHON = Path(os.environ.get("ECHO_VENV", firmware.VENV_DEFAULT)).resolve() / "bin/python"
CHANGELOG_HEADING = re.compile(r"^## \[([^\]]+)\] - (Unreleased|\d{4}-\d{2}-\d{2})$", re.M)
COMMIT = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")


def run(*args, **kwargs):
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def git(*args):
    return run("git", *args, stdout=subprocess.PIPE, text=True).stdout.strip()


def changelog(version, path):
    text = path.read_text()
    entries = list(CHANGELOG_HEADING.finditer(text))
    for index, entry in enumerate(entries):
        if "v" + entry[1] == version:
            end = entries[index + 1].start() if index + 1 < len(entries) else len(text)
            notes = re.split(r"^\[[^\]]+\]:", text[entry.end():end], maxsplit=1, flags=re.M)[0].strip()
            if notes:
                published = {"v" + item[1] for item in entries if item[2] != "Unreleased"}
                return notes, published
    raise ValueError(f"{path.name}: missing changelog entry for {version}")


def check_site(version, site, published):
    version_name = version[1:]
    comparison = (site / "comparison.html").read_text()
    headings = re.findall(rf'data-player="{re.escape(PROJECT)}">[^<]+<small>ECHO · ([^<]+)</small>', comparison)
    intro = re.findall(r'class="intro">[^\n]+?' + re.escape(RELEASES) + r'/tag/(v[^"]+)">([^ <]+)',
                       comparison)
    if headings != [version_name, version_name] or intro != [(version, version_name)]:
        raise ValueError(f"comparison.html: the intro and both firmware headings must name {version}")

    install = (site / "install.html").read_text()
    direct = re.findall(re.escape(RELEASES) + r'/download/(v[^"/]+)/([^"?#]+)', install)
    download_names = {package.STOCK_IMAGE, package.SUMS}
    pins = {tag for tag, _ in direct}
    if (len(direct) != len(download_names) or {name for _, name in direct} != download_names
            or len(pins) != 1 or not pins <= published | {version}):
        raise ValueError("install.html: direct firmware links must share a known release tag")
    installed = pins.pop()
    labels = {"install.html": f"Download Firmware {installed} directly",
              "languages.html": f"Ready to install · {installed}"}
    for page, text in (("install.html", install), ("languages.html", (site / "languages.html").read_text())):
        links = re.findall(re.escape(RELEASES) + r'/tag/(v[^"?#]+)', text)
        if set(links) != {installed} or labels[page] not in text:
            raise ValueError(f"{page}: firmware and language pack links must name {installed}")

    catalog = json.loads((site / "themes-catalog.json").read_text())
    official = {item["id"]: item for item in catalog["themes"] if item["id"] in publish_site.STUDIO_EXAMPLES}
    for name in THEMES:
        item = official.get(Path(name).stem, {})
        if item.get("downloadUrl") != f"{RELEASES}/download/{version}/{name}":
            raise ValueError(f"themes-catalog.json: {name} must link to {version}")
        if not SHA256.fullmatch(item.get("sha256", "")):
            raise ValueError(f"themes-catalog.json: {name} needs a SHA-256")
    return {f"{slug}.gtheme": item["sha256"] for slug, item in official.items()}


def check_candidate(version):
    notes, published = changelog(version, ROOT / "CHANGELOG.md")
    return notes, check_site(version, ROOT / "site", published)


def asset_names():
    return {package.IMAGE, package.STOCK_IMAGE, package.source_name(), package.MANIFEST, package.SUMS,
            *THEMES, *(f"{code}.pack" for code in package.language_codes(package.LANGUAGES))}


def check_theme(path):
    run(PYTHON, ROOT / "firmware/tools/theme_pack.py", "--check", path, stdout=subprocess.DEVNULL)


def verify_assets(directory, version, commit, theme_hashes):
    manifest = package.verify(directory, version, commit, extra_assets=THEMES)
    build = package.label(version) or commit[:7]
    if manifest.get("version") != package.IDENTITY["version"] or manifest.get("model") != package.MODEL:
        raise ValueError(f"{package.MANIFEST}: wrong firmware version or model")
    if manifest["build"] != build:
        raise ValueError(f"{package.IMAGE}: expected build {build!r}")
    for name in THEMES:
        check_theme(directory / name)
        if theme_hashes and manifest["files"][name]["sha256"] != theme_hashes[name]:
            raise ValueError(f"{name}: SHA-256 differs from themes-catalog.json")
    return manifest


def clean_head():
    if git("status", "--porcelain"):
        raise ValueError("Commit the release source before packaging or deploying the site")
    return git("rev-parse", "HEAD")


def scratch_tmp():
    if not firmware.SCRATCH.parent.is_mount():
        raise ValueError("Mount Scratch before preparing or downloading release assets")
    temporary = firmware.SCRATCH / "tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    return temporary


def staging_directory(version, commit, directory):
    path = (directory or firmware.SCRATCH / "releases" / version / commit).resolve()
    if not path.is_relative_to(firmware.SCRATCH.resolve()) or path == firmware.SCRATCH.resolve():
        raise ValueError("Use a staging directory under the project's Scratch folder")
    return path


def prepare(version, build_label, directory, jobs, theme_hashes):
    commit = clean_head()
    temporary = scratch_tmp()
    output = staging_directory(version, commit, directory)
    if output.exists():
        raise ValueError(f"{output} already exists. Choose a new --directory for a fresh package")
    env = {**os.environ, "TMPDIR": str(temporary), "PYTHONDONTWRITEBYTECODE": "1",
           "ECHO_BUILD_TAG": build_label, "SOURCE_DATE_EPOCH": git("show", "-s", "--format=%ct", commit)}
    run(sys.executable, ROOT / "tools/publish_site.py", "--check", env=env, stdout=subprocess.DEVNULL)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".prepare-", dir=output.parent) as work:
        work = Path(work)
        run(sys.executable, ROOT / "tools/firmware.py", "sizes", "--jobs", str(jobs), env=env)
        candidate = work / "candidate"
        for preset in PRESETS:
            (candidate / preset).mkdir(parents=True)
            shutil.copyfile(ROOT / "firmware/build" / preset / package.IMAGE,
                            candidate / preset / package.IMAGE)
        for name in THEMES:
            run(PYTHON, ROOT / "firmware/tools/theme_pack.py", ROOT / "examples/themes" / Path(name).stem,
                "--output", work / name, env=env)
        source = work / package.source_name()
        git("archive", "--format=tar.gz", f"--prefix={PROJECT}/",
            "--output", source, commit)
        if clean_head() != commit:
            raise ValueError("The checkout changed during preparation")
        assets = candidate / "assets"
        image = candidate / package.preset(build_label) / package.IMAGE
        package.package(image, source, assets, commit, version, extra_assets=tuple(work / name for name in THEMES))
        verify_assets(assets, version, commit, theme_hashes)
        candidate.rename(output)
    print(output)


def gh_json(*args):
    return json.loads(run("gh", "api", "--hostname", "github.com", *args, stdout=subprocess.PIPE).stdout)


def gh_release(*args):
    run("gh", "release", *args, "--repo", "github.com/" + REPO)


def find_release(version):
    pages = gh_json(f"{API}?per_page=100", "--paginate", "--slurp")
    matches = [entry for page in pages for entry in page if entry["tag_name"] == version]
    if len(matches) > 1:
        raise ValueError(f"Multiple GitHub releases found for {version}")
    return matches[0] if matches else None


def check_remote_tag(version, commit):
    refs = gh_json(f"repos/{REPO}/git/matching-refs/tags/{version}")
    if any(entry["ref"] == f"refs/tags/{version}" for entry in refs):
        resolved = gh_json(f"repos/{REPO}/commits/{version}")["sha"]
        if resolved != commit:
            raise ValueError(f"{version} already points to {resolved}, expected {commit}")


def channel_flags(prerelease):
    return ("--prerelease", "--latest=false") if prerelease else ("--prerelease=false",)


def stage(version, prerelease, directory, notes, theme_hashes):
    commit = clean_head()
    scratch_tmp()
    assets = staging_directory(version, commit, directory) / "assets"
    verify_assets(assets, version, commit, theme_hashes)
    release = find_release(version)
    if release is not None and not release["draft"]:
        raise ValueError(f"{version} is already published")
    check_remote_tag(version, commit)
    title = f"{package.IDENTITY['name']} {version}"
    settings = ("--title", title, "--notes", notes, "--target", commit,
                *channel_flags(prerelease))
    files = sorted(assets.iterdir())
    if release is None:
        gh_release("create", version, "--draft", *settings, *files)
    else:
        gh_release("edit", version, *settings)
        gh_release("upload", version, "--clobber", *files)


def remote_assets(release):
    pages = gh_json(f"{API}/{release['id']}/assets?per_page=100", "--paginate", "--slurp")
    assets = {asset["name"]: asset for page in pages for asset in page}
    package.check_names("GitHub release assets", assets.keys(), asset_names())
    for name, asset in assets.items():
        if asset["state"] != "uploaded" or type(asset["id"]) is not int:
            raise ValueError(f"{name}: GitHub asset is not uploaded")
    return assets


def smoke(version, release, theme_hashes):
    commit = release.get("target_commitish", "")
    if not COMMIT.fullmatch(commit):
        raise ValueError(f"{version}: GitHub release must target a full source commit SHA")
    check_remote_tag(version, commit)
    assets = remote_assets(release)
    with tempfile.TemporaryDirectory(prefix="release-smoke-", dir=scratch_tmp()) as work:
        directory = Path(work)
        for name, asset in assets.items():
            with (directory / name).open("wb") as stream:
                run("gh", "api", "--hostname", "github.com", f"{API}/assets/{asset['id']}",
                    "-H", "Accept: application/octet-stream", stdout=stream)
            if (directory / name).stat().st_size != asset["size"]:
                raise ValueError(f"{name}: downloaded size differs from GitHub")
        verify_assets(directory, version, commit, theme_hashes)
    print(f"Verified {len(assets)} assets for {version} at {commit}")


def promote(version, release, prerelease, deploy_site, theme_hashes):
    if not release["draft"] and (not deploy_site or release["prerelease"] != prerelease):
        raise ValueError(f"{version} is already published")
    publisher = (sys.executable, ROOT / "tools/publish_site.py")
    if deploy_site:
        if clean_head() != release["target_commitish"]:
            raise ValueError("Check out the release commit before deploying its site")
        run(*publisher, "--check", stdout=subprocess.DEVNULL)
    smoke(version, release, theme_hashes)
    if release["draft"]:
        gh_release("edit", version, "--draft=false", *channel_flags(prerelease),
                   *(() if prerelease else ("--latest",)))
    if deploy_site:
        run(*publisher)


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for action in ("prepare", "stage", "smoke", "promote"):
        command = commands.add_parser(action)
        command.add_argument("version", help="Firmware tag beginning with v")
        command.add_argument("--dry-run", action="store_true", help="Validate local release references and print the plan")
        if action in ("prepare", "stage"):
            command.add_argument("--directory", type=Path, help="Override the staging directory on Scratch")
        if action == "prepare":
            command.add_argument("--jobs", type=int, choices=range(1, 17),
                                 default=int(os.environ.get("COMPUTE_JOBS", "4")))
        if action == "promote":
            command.add_argument("--channel", choices=("nightly", "stable"))
            command.add_argument("--deploy-site", action="store_true")
    return parser.parse_args(argv)


def main(argv=None):
    args = arguments(argv)
    version = args.version
    try:
        build_label = package.label(version)
        notes, theme_hashes = check_candidate(version)
        channel = getattr(args, "channel", None) or ("nightly" if build_label else "stable")
        deploy_site = getattr(args, "deploy_site", False)
        if args.dry_run:
            print(json.dumps({"action": args.command, "version": version, "channel": channel,
                              "deploy_site": deploy_site, "assets": sorted(asset_names())}))
        elif args.command == "prepare":
            prepare(version, build_label, args.directory, args.jobs, theme_hashes)
        elif args.command == "stage":
            stage(version, channel == "nightly", args.directory, notes, theme_hashes)
        else:
            release = find_release(version)
            if release is None or (args.command == "smoke" and not release["draft"]):
                raise ValueError(f"No GitHub draft release for {version}")
            if args.command == "smoke":
                smoke(version, release, theme_hashes)
            else:
                promote(version, release, channel == "nightly", deploy_site, theme_hashes)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
