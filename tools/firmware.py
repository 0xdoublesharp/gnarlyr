#!/usr/bin/env python3
"""Build, regenerate and check firmware; install-device explicitly accesses the player."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware"
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")
ARM_DEFAULT = SCRATCH / "tools/arm-15.3-expanded/Payload/bin"
VENV_DEFAULT = SCRATCH / "tools/podium-python314"
BUILD_NAME = ROOT.name if (ROOT / ".git").is_file() else "custom-firmware"
GROUPS = {
    "host": "^test_",
    "smoke": "^(test_player|test_settings|test_up_next|test_session_journal|"
             "device_image_passes_the_updater_check|"
             "device_image_boot_test_normal_boot_reaches_the_home_screen_using_only_the_user_volume|"
             "device_image_boot_test_a_damaged_module_in_slot_1_is_replaced_by_the_copy_in_slot_2)$",
}
FUZZ_TARGETS = {
    "track_tags": 65536,
    "playlist_reader": 32768,
    "session_journal": 4096,
    "library_database": 65536,
    "mp4_demux": 262144,
    "store": 4096,
    "settings_codec": 128,
    "cue_sheet": 65536,
}
FUZZ_SEEDS = {
    "track_tags": [b"\0"],
    "playlist_reader": [
        b"\xff#EXTM3U\n../A/b.flac\n",
        b"\x1f\xef\xbb\xbfA/one.flac\xe2\x80\xa8A/two.flac\n",
        b"\x03Bj\xf6rk/J\xf3ga.flac\x85end.flac\n",
    ],
    "session_journal": [b"\0", bytes(range(64)), b"\xff" * 64],
    "library_database": [b"\0", b"\0" * 128],
    "mp4_demux": [b"\0"],
    "store": [
        b"\0" * 16,
        bytes(range(32)),
        b"\xff\x01\xff\x01\0\x02\0\x02" * 4,
    ],
    "settings_codec": [
        bytes(range(128)),
        b"\1" + bytes(range(32)),
        b"\2" + bytes(range(64)),
        b"\3" + b"\xff" * 32,
    ],
    "cue_sheet": [
        b'FILE "a.flac" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n'
        b"TRACK 02 AUDIO\nINDEX 01 03:20:00\n",
        b"FILE a.flac WAVE\nTRACK 00 AUDIO\nINDEX 01 00:00:00\n"
        b"TRACK 100 AUDIO\nINDEX 01 00:01:00\nTRACK AUDIO\nINDEX 01 00:02:00\n",
        b"FILE a.flac WAVE\nTRACK 01 AUDIO\nINDEX 01 00:60:00\n"
        b"TRACK 02 AUDIO\nINDEX 01 00:00:75\nTRACK 03 AUDIO\nINDEX 01 00:00:00 trailing\n",
        b"FILE a.flac WAVE\nTRACK 01 AUDIO\nINDEX 01 00:10:00\n"
        b"TRACK 02 AUDIO\nINDEX 01 00:05:00\nTRACK 03 AUDIO\nINDEX 01 00:10:00\n",
    ],
}
FUZZ_FIXTURES = {
    "library_database": [
        SOURCE / "tests/fixtures/small_library.db",
        SOURCE / "tests/fixtures/cue_music.db",
        SOURCE / "tests/fixtures/builder_random/library.db",
        SOURCE / "tests/fixtures/builder_random/with_playlists.db",
    ],
    "mp4_demux": [
        SOURCE / "tests/fixtures/tags/aac.m4a",
        SOURCE / "tests/fixtures/tags/alac.m4a",
        SOURCE / "tests/fixtures/m4a_music/Aac Artist/Aac Album/01 Lead.m4a",
        SOURCE / "tests/fixtures/m4a_music/Aac Artist/Aac Album/02 Tail.m4a",
        SOURCE / "tests/fixtures/m4a_music/Aac Artist/Aac Album/03 Mono.m4a",
        SOURCE / "tests/fixtures/m4a_music/Alac Artist/Alac Album/01 Lossless.m4a",
        SOURCE / "tests/fixtures/m4a_music/Alac Artist/Alac Album/02 Deep.m4a",
        SOURCE / "tests/fixtures/m4a_music/Alac Artist/Alac Album/03 Single.m4a",
    ],
}


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
            raise SystemExit(f"Missing prerequisite: {path}; see README.md")
    version = subprocess.check_output([str(python), "-c", "import sys; print('%d.%d' % sys.version_info[:2])"], text=True).strip()
    if version != "3.14":
        raise SystemExit(f"Python 3.14 is required for generated Unicode tables, got {version}")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(arm), "/opt/homebrew/opt/llvm/bin", str(venv / "bin"), env.get("PATH", "")])
    env["TMPDIR"] = str(SCRATCH / "tmp" / BUILD_NAME)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(jobs)
    env["SCCACHE_DIR"] = "/Volumes/Scratch/sccache"
    for name in ("cmake", "ctest", "clang-format", "ffmpeg"):
        if not shutil.which(name, path=env["PATH"]):
            raise SystemExit(f"Missing prerequisite: {name}; see README.md")
    subprocess.run([str(python), "-c", "import PIL, fontTools, mutagen, unicorn"], check=True, env=env)
    build = SCRATCH / "build" / BUILD_NAME
    build.mkdir(parents=True, exist_ok=True)
    Path(env["TMPDIR"]).mkdir(parents=True, exist_ok=True)
    link_directory(SOURCE / "build", build)
    link_directory(SOURCE / ".venv", venv)
    return env, python


def run(command, env):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=SOURCE, env=env, check=True)


def configure(preset, env, python):
    if preset == "dev":
        query = SOURCE / "build/dev/.cmake/api/v1/query/codemodel-v2"
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()
    command = ["cmake", "--preset", preset, f"-DPython3_EXECUTABLE={python}"]
    if preset == "fuzz":
        compiler = shutil.which("clang", path=env["PATH"])
        if compiler is None:
            raise SystemExit("Missing prerequisite: clang; see README.md")
        command.append(f"-DCMAKE_C_COMPILER={compiler}")
    launcher = shutil.which("sccache", path=env["PATH"]) if env.get("ECHO_COMPILER_CACHE", "1") != "0" else None
    command.append(f"-DCMAKE_C_COMPILER_LAUNCHER={launcher or ''}")
    run(command, env)


def compile_targets(preset, env, targets=None):
    if targets == []:
        return
    command = ["cmake", "--build", "--preset", preset]
    if targets is not None:
        command += ["--target", *targets]
    run(command, env)


def target_artifacts():
    build_dir = SOURCE / "build/dev"
    reply = build_dir / ".cmake/api/v1/reply"
    indexes = sorted(reply.glob("index-*.json"))
    index = json.loads(indexes[-1].read_text())
    model = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text())
    artifacts = {}
    for entry in model["configurations"][0]["targets"]:
        target = json.loads((reply / entry["jsonFile"]).read_text())
        if target["type"] == "EXECUTABLE":
            for artifact in target.get("artifacts", []):
                artifacts[(build_dir / artifact["path"]).resolve()] = target["name"]
    return artifacts


def build_targets(tests, artifacts):
    if not tests:
        raise ValueError("No tests matched; check --regex or --group")
    targets = set()
    for test in tests:
        command = test.get("command")
        if not command:
            if test["name"] not in artifacts.values():
                return None
            targets.add(test["name"])
        else:
            for argument in command:
                if Path(argument).is_absolute():
                    target = artifacts.get(Path(argument).resolve())
                    if target:
                        targets.add(target)
    return sorted(targets)


def selected_tests(regex, env):
    command = ["ctest", "--preset", "dev", "--show-only=json-v1"]
    if regex:
        command += ["-R", regex]
    output = subprocess.check_output(command, cwd=SOURCE, env=env, text=True)
    tests = json.loads(output)["tests"]
    if not tests:
        raise ValueError("No tests matched; check --regex or --group")
    return tests


def fuzz_command(binary, corpus, artifacts, runs, seed, max_length):
    return [
        binary,
        f"-runs={runs}",
        f"-seed={seed}",
        f"-max_len={max_length}",
        "-timeout=2",
        "-rss_limit_mb=2048",
        "-print_final_stats=1",
        f"-artifact_prefix={artifacts}/",
        corpus,
    ]


def write_seed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prepare_fuzz_corpus(name):
    corpus = SCRATCH / "data" / BUILD_NAME / "fuzz" / name
    corpus.mkdir(parents=True, exist_ok=True)
    for index, seed in enumerate(FUZZ_SEEDS[name]):
        write_seed(corpus / f"seed-{index}", seed)
    for fixture in FUZZ_FIXTURES.get(name, ()):
        write_seed(corpus / f"fixture-{fixture.name}", fixture.read_bytes()[:FUZZ_TARGETS[name]])
    if name == "track_tags":
        fixtures = SOURCE / "tests/fixtures/tags"
        for fixture in sorted(fixtures.iterdir()):
            if fixture.is_file():
                write_seed(corpus / f"tag-{fixture.name}", fixture.read_bytes()[:FUZZ_TARGETS[name]])
    return corpus


def run_fuzzer(name, runs, seed, env):
    corpus = prepare_fuzz_corpus(name)
    artifacts = SCRATCH / "reports" / BUILD_NAME / "fuzz" / name
    artifacts.mkdir(parents=True, exist_ok=True)
    binary = SOURCE / "build/fuzz/tests/fuzz" / f"fuzz_{name}"
    run(fuzz_command(binary, corpus, artifacts, runs, seed, FUZZ_TARGETS[name]), env)


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("setup", "build", "test", "sim", "fuzz", "regen", "sizes", "install-device"))
    parser.add_argument("--check", action="store_true", help="regen: fail if generated sources or assets are stale")
    parser.add_argument("--existing", action="store_true", help="sizes: read existing release/device builds without rebuilding")
    parser.add_argument("--dry-run", action="store_true", help="install-device: freeze, prove and back up without installing")
    parser.add_argument("--owner-approved", action="store_true", help="install-device: confirm owner approval for this installation and exact-model recovery readiness")
    parser.add_argument("--preset", choices=("dev", "fuzz", "device", "release"), default="dev")
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=4)
    parser.add_argument("--build-jobs", type=int, choices=range(1, 17), help="Override build parallelism")
    parser.add_argument("--test-jobs", type=int, choices=range(1, 17), help="Override test parallelism")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--regex", help="CTest test-name expression; build only selected executables")
    selection.add_argument("--group", choices=GROUPS, help="host: native unit tests; smoke: native and ARM integration")
    parser.add_argument("--list", action="store_true", help="List selected tests and build targets without compiling")
    parser.add_argument("--target", action="append", help="Build or fuzz one target; repeat for several targets")
    parser.add_argument("--runs", type=int, help="Fuzzer executions per target")
    parser.add_argument("--seed", type=int, help="Deterministic fuzzer seed")
    parser.add_argument("--card", type=Path, help="Simulator music directory")
    args = parser.parse_args()
    for enabled, action, option in ((args.check, "regen", "--check"), (args.existing, "sizes", "--existing"),
                                   (args.dry_run or args.owner_approved, "install-device", "--dry-run/--owner-approved")):
        if enabled and args.action != action:
            parser.error(f"{option} requires {action}")
    if (args.regex is not None or args.group or args.list or args.test_jobs) and args.action != "test":
        parser.error("--regex, --group, --list and --test-jobs require the test action")
    if args.target and args.action not in ("build", "fuzz"):
        parser.error("--target requires the build or fuzz action")
    if args.action == "fuzz":
        unknown = sorted(set(args.target or ()) - set(FUZZ_TARGETS))
        if unknown:
            parser.error(f"unknown fuzz target: {', '.join(unknown)}")
        if args.runs is not None and args.runs < 1:
            parser.error("--runs must be positive")
        if args.seed is not None and args.seed < 1:
            parser.error("--seed must be positive")
    elif args.runs is not None or args.seed is not None:
        parser.error("--runs and --seed require the fuzz action")
    if args.action == "sim" and (not args.card or not args.card.is_dir()):
        parser.error("sim requires --card pointing to a music directory")
    return parser, args


def main():
    parser, args = arguments()
    env, python = environment(args.build_jobs or args.jobs)
    if args.action in ("regen", "sizes", "install-device"):
        from firmware_tasks import print_sizes, regenerate

        if args.action == "regen":
            regenerate(SOURCE, env, python, args.check)
        elif args.action == "sizes":
            if not args.existing:
                for name in ("release", "device"):
                    configure(name, env, python)
                    compile_targets(name, env, ["echo_device_image"])
            print_sizes(SOURCE)
        else:
            from device_release import Release

            Release(ROOT, SCRATCH, env, python).execute(args.dry_run, args.owner_approved)
        return
    if args.action == "setup":
        print(f"Source: {SOURCE}; output: {SOURCE / 'build'} -> {(SOURCE / 'build').resolve()}")
        return
    preset = "dev" if args.action in ("test", "sim") else args.preset
    if args.action == "fuzz":
        targets = args.target or list(FUZZ_TARGETS)
        configure("fuzz", env, python)
        compile_targets("fuzz", env, [f"fuzz_{name}" for name in targets])
        runs = args.runs if args.runs is not None else 10000
        seed = args.seed if args.seed is not None else 1
        for name in targets:
            run_fuzzer(name, runs, seed, env)
        return
    configure(preset, env, python)
    if args.action == "test":
        regex = GROUPS.get(args.group, args.regex)
        try:
            tests = selected_tests(regex, env)
            targets = build_targets(tests, target_artifacts()) if regex else None
        except ValueError as error:
            parser.error(str(error))
        if args.list:
            for test in tests:
                print(test["name"])
            print("Build targets:", "all" if targets is None else ", ".join(targets) or "none")
            return
        compile_targets("dev", env, targets)
        command = ["ctest", "--preset", "dev", "--output-on-failure", "--no-tests=error",
                   "-j", str(args.test_jobs or args.jobs)]
        if regex:
            command += ["-R", regex]
        run(command, env)
    elif args.action == "sim":
        compile_targets("dev", env, ["echo_sim"])
        system = SCRATCH / "data" / BUILD_NAME / "sim-system"
        system.mkdir(parents=True, exist_ok=True)
        run([SOURCE / "build/dev/platform/sim/echo_sim", "--card", args.card.resolve(), "--system-dir", system], env)
    else:
        compile_targets(preset, env, args.target)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
