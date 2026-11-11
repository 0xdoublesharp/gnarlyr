"""Records the ELF and map files a device image was built from and removes stale ones."""
import argparse
import json
import sys
from pathlib import Path

MANIFEST = "image-artifacts.json"
STALE_PATTERNS = ("echo_*.elf", "echo_*.map")


def with_maps(elfs):
    return [path for elf in elfs for path in (elf, elf.with_suffix(".map"))]


def stale_files(directories, kept):
    keep = {path.resolve() for path in kept}
    return sorted(path for directory in directories for pattern in STALE_PATTERNS
                  for path in directory.glob(pattern) if path.resolve() not in keep)


def manifest(build, elfs, modules):
    return {"files": [str(path.relative_to(build)) for path in with_maps(elfs + modules)],
            "modules": [str(path.relative_to(build)) for path in modules]}


def read_manifest(build):
    path = build / MANIFEST
    return json.loads(path.read_text()) if path.is_file() else None


def module_elfs(build):
    recorded = read_manifest(build)
    if recorded is None:
        return sorted((build / "platform/device").glob("echo_module_*.elf"))
    return [build / name for name in recorded["modules"]]


def record(build, elfs, modules):
    kept = with_maps(elfs + modules)
    missing = [str(path) for path in kept if not path.is_file()]
    if missing:
        raise ValueError("Missing build artifacts: " + ", ".join(missing))
    removed = stale_files({path.parent for path in kept}, kept)
    for path in removed:
        path.unlink()
        print(f"removed stale {path.relative_to(build)}")
    (build / MANIFEST).write_text(json.dumps(manifest(build, elfs, modules), indent=2) + "\n")
    return removed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True, help="the preset's build directory")
    parser.add_argument("--elf", type=Path, action="append", default=[], help="a program the image is built from")
    parser.add_argument("--module", type=Path, action="append", default=[], help="a module ELF, in module order")
    arguments = parser.parse_args(argv)
    build = arguments.build.resolve()
    record(build, [path.resolve() for path in arguments.elf], [path.resolve() for path in arguments.module])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as error:
        raise SystemExit(str(error)) from None
