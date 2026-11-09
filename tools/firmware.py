#!/usr/bin/env python3
"""Build and run the ECHO firmware locally; no player access."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware"
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")
ARM_DEFAULT = SCRATCH / "tools/arm-15.3-expanded/Payload/bin"
VENV_DEFAULT = SCRATCH / "tools/podium-python314"


def link_directory(path, target):
    if path.is_symlink():
        if path.resolve() != target.resolve():
            raise SystemExit(f"{path} points elsewhere; preserve it and resolve the conflict")
    elif path.exists():
        raise SystemExit(f"{path} already exists; move its generated output to Scratch first")
    else:
        path.symlink_to(target, target_is_directory=True)


def environment(jobs):
    if not os.path.ismount("/Volumes/Scratch"):
        raise SystemExit("Scratch is not mounted; no build output was written")
    arm = Path(os.environ.get("ECHO_ARM_BIN", str(ARM_DEFAULT))).resolve()
    venv = Path(os.environ.get("ECHO_VENV", str(VENV_DEFAULT))).resolve()
    python = venv / "bin/python"
    for path in (arm / "arm-none-eabi-gcc", arm / "arm-none-eabi-objcopy", python):
        if not path.is_file():
            raise SystemExit(f"Missing prerequisite: {path}; see docs/building-firmware.md")
    version = subprocess.check_output([str(python), "-c", "import sys; print('%d.%d' % sys.version_info[:2])"], text=True).strip()
    if version != "3.14":
        raise SystemExit(f"Python 3.14 is required for generated Unicode tables, got {version}")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(arm), "/opt/homebrew/opt/llvm/bin", str(venv / "bin"), env.get("PATH", "")])
    env["TMPDIR"] = str(SCRATCH / "tmp/custom-firmware")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(jobs)
    env["SCCACHE_DIR"] = "/Volumes/Scratch/sccache"
    for name in ("cmake", "ctest", "clang-format", "ffmpeg"):
        if not shutil.which(name, path=env["PATH"]):
            raise SystemExit(f"Missing prerequisite: {name}; see docs/building-firmware.md")
    subprocess.run([str(python), "-c", "import PIL, fontTools, mutagen, unicorn"], check=True, env=env)
    build = SCRATCH / "build/custom-firmware"
    build.mkdir(parents=True, exist_ok=True)
    Path(env["TMPDIR"]).mkdir(parents=True, exist_ok=True)
    link_directory(SOURCE / "build", build)
    link_directory(SOURCE / ".venv", venv)
    return env, python


def run(command, env):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=SOURCE, env=env, check=True)


def build(preset, env, python):
    command = ["cmake", "--preset", preset, f"-DPython3_EXECUTABLE={python}"]
    launcher = shutil.which("sccache", path=env["PATH"]) if env.get("ECHO_COMPILER_CACHE", "0") == "1" else None
    command.append(f"-DCMAKE_C_COMPILER_LAUNCHER={launcher or ''}")
    run(command, env)
    run(["cmake", "--build", "--preset", preset], env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("setup", "build", "test", "sim"))
    parser.add_argument("--preset", choices=("dev", "device", "release"), default="dev")
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=4)
    parser.add_argument("--regex", help="CTest test-name expression")
    parser.add_argument("--card", type=Path, help="Simulator music directory")
    args = parser.parse_args()
    env, python = environment(args.jobs)
    if args.action == "setup":
        print(f"Source: {SOURCE}; output: {SOURCE / 'build'} -> {(SOURCE / 'build').resolve()}")
        return
    build("dev" if args.action in ("test", "sim") else args.preset, env, python)
    if args.action == "test":
        command = ["ctest", "--preset", "dev", "--output-on-failure", "-j", str(args.jobs)]
        if args.regex:
            command += ["-R", args.regex]
        run(command, env)
    elif args.action == "sim":
        if not args.card or not args.card.is_dir():
            parser.error("sim requires --card pointing to a music directory")
        system = SCRATCH / "data/custom-firmware/sim-system"
        system.mkdir(parents=True, exist_ok=True)
        run([SOURCE / "build/dev/platform/sim/echo_sim", "--card", args.card.resolve(), "--system-dir", system], env)


if __name__ == "__main__":
    main()
