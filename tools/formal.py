#!/usr/bin/env python3
"""Run the on-demand formal checks: CBMC harnesses for the card-file readers and TLC on the TLA+ models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import time
import urllib.request

from echo_debug import IDENTITY

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware"
FORMAL = SOURCE / "formal"
MANIFEST = FORMAL / "checks.json"
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")
BUILD_NAME = ROOT.name if (ROOT / ".git").is_file() else SCRATCH.name
TLA_JAR = SCRATCH / "tools" / "tla" / "tla2tools.jar"
CBMC_TOOLS = ("goto-cc", "goto-instrument", "cbmc")
IDENTITY_FIELDS = ("name", "short_name", "manufacturer", "id", "version", "data_directory", "update_file",
                   "playlist_start_directive")
FAILED_PROPERTY = re.compile(r"^\[([^\]]+)\] .*: FAILURE$", re.MULTILINE)
COVER_GOAL = re.compile(r"^\[\S+\.coverage\.\d+\] file (\S+) line \d+ function \S+ block .*: (SATISFIED|FAILED)$",
                        re.MULTILINE)
NO_BODY = re.compile(r"no body for (?:callee|function) (\S+)")
FAILED_COUNT = re.compile(r"\*\* (\d+) of (\d+) failed")
TLC_STATES = re.compile(r"([\d,]+) states generated, ([\d,]+) distinct states found")
TLC_DEPTH = re.compile(r"The depth of the complete state graph search is (\d+)")
EXIT_MISSING_TOOLS = 2
EXIT_FAILED = 1


class Outcome:
    def __init__(self, name, kind, status, seconds, detail, log):
        self.name = name
        self.kind = kind
        self.status = status
        self.seconds = seconds
        self.detail = detail
        self.log = log
        self.cpu = None

    def as_dict(self):
        cpu = None if self.cpu is None else round(self.cpu, 1)
        return {"name": self.name, "kind": self.kind, "status": self.status,
                "seconds": round(self.seconds, 1), "cpu_seconds": cpu, "detail": self.detail,
                "log": str(self.log)}


def child_cpu_seconds():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def run_tool(command, cwd, env, timeout):
    try:
        done = subprocess.run([str(part) for part in command], cwd=cwd, env=env, timeout=timeout,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        return done.returncode, done.stdout
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        return None, output


def load_manifest(path=MANIFEST):
    return json.loads(path.read_text(encoding="utf-8"))


def firmware_definitions(identity=IDENTITY):
    definitions = [f'-DFIRMWARE_{field.upper()}="{identity[field]}"' for field in IDENTITY_FIELDS]
    revision = re.match(r"^[0-9]+\.[0-9]+", identity["version"])
    if revision is None:
        raise SystemExit(f"identity.json: version {identity['version']!r} does not start with major.minor")
    definitions.append(f'-DFIRMWARE_REVISION="{revision.group(0)}"')
    return definitions


def include_directories(source=SOURCE):
    folders = sorted(source.glob("src/*/include")) + [source / "platform/device/logic/include"]
    return [f"-I{folder}" for folder in folders]


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def cbmc_install_steps():
    return ("CBMC (goto-cc, goto-instrument, cbmc) is not on PATH. Install it:\n"
            "  macOS:  brew install cbmc\n"
            "  Ubuntu: download the cbmc .deb for your release from https://github.com/diffblue/cbmc/releases\n"
            "          and install it with sudo apt-get install ./ubuntu-<version>-cbmc-<release>-Linux.deb")


def tla_install_steps(tla):
    return (f"TLC needs Java 11 or later on PATH and tla2tools.jar {tla['version']} at {TLA_JAR}.\n"
            "  Java:  brew install openjdk@21 (macOS) or sudo apt-get install openjdk-21-jre-headless\n"
            "  Jar:   python3 tools/formal.py --fetch-tla\n"
            f"         (downloads {tla['url']}\n"
            f"          and checks SHA-1 {tla['sha1']} from the release notes and SHA-256 {tla['sha256']})")


def missing_cbmc(which=shutil.which):
    return [tool for tool in CBMC_TOOLS if which(tool) is None]


def tla_problem(tla, which=shutil.which, jar=None, digest=sha256_of):
    jar = TLA_JAR if jar is None else jar
    if which("java") is None:
        return "java is not on PATH"
    if not Path(jar).is_file():
        return f"{jar} is missing"
    if digest(jar) != tla["sha256"]:
        return f"{jar} does not match the pinned SHA-256 {tla['sha256']}"
    return None


def fetch_tla(tla, jar=None, opener=urllib.request.urlopen):
    jar = Path(TLA_JAR if jar is None else jar)
    jar.parent.mkdir(parents=True, exist_ok=True)
    partial = jar.with_suffix(".part")
    with opener(tla["url"]) as response, open(partial, "wb") as stream:
        shutil.copyfileobj(response, stream)
    data = partial.read_bytes()
    if hashlib.sha1(data).hexdigest() != tla["sha1"] or hashlib.sha256(data).hexdigest() != tla["sha256"]:
        partial.unlink()
        raise SystemExit(f"Downloaded tla2tools.jar does not match the pinned checksums; nothing was kept")
    partial.replace(jar)
    return jar


def cbmc_compile_command(harness, output, source=SOURCE):
    stubs = FORMAL / "cbmc" / "stubs"
    files = [source / name for name in harness["sources"]]
    files += [stubs / name for name in harness.get("stubs", [])]
    files.append(FORMAL / "cbmc" / "harnesses" / f"{harness['entry']}.c")
    defines = [f"-D{key}={value}" for key, value in sorted(harness.get("defines", {}).items())]
    includes = [f"-I{source / folder}" for folder in harness.get("includes", [])]
    includes += [f"-I{stubs}"] + include_directories(source)
    for header in harness.get("preinclude", []):
        includes += ["-include", str(stubs / header)]
    return ["goto-cc", "-m32", "-funsigned-char", "-ffreestanding", "-std=c11", *firmware_definitions(), *defines, *includes,
            *map(str, files), "-o", str(output)]


def cbmc_cover_command(harness, program):
    return ["cbmc", str(program), "--function", "harness", "--cover", "location", "--unwind",
            str(harness["unwind"]), *[flag for flag in harness.get("checks", []) if flag != "--unsigned-overflow-check"]]


def coverage_by_file(output, harness, source=SOURCE):
    wanted = {str(source / name): name for name in harness["sources"]}
    counts = {}
    for path, verdict in COVER_GOAL.findall(output):
        if path in wanted:
            reached, total = counts.get(wanted[path], (0, 0))
            counts[wanted[path]] = (reached + (verdict == "SATISFIED"), total + 1)
    return counts


def coverage_text(counts):
    return ", ".join(f"{Path(name).name} {reached}/{total}" for name, (reached, total) in sorted(counts.items())
                     if reached)


def cbmc_check_command(manifest, harness, program):
    command = ["cbmc", str(program), "--function", "harness", *manifest["cbmc"]["checks"],
               *harness.get("checks", []), "--unwind", str(harness["unwind"])]
    command.append("--unwinding-assertions" if harness.get("unwinding_assertions", True) else "--no-unwinding-assertions")
    if harness.get("unwindset"):
        command += ["--unwindset", harness["unwindset"]]
    return command


def unexpected_bodies(output):
    names = {name.strip("'\":") for name in NO_BODY.findall(output)}
    return sorted(name for name in names if not name.startswith("nondet_"))


def judge_cbmc(code, output, harness):
    if code is None:
        return "timeout", "stopped at the time limit"
    missing = unexpected_bodies(output)
    if missing:
        return "error", "no body for " + ", ".join(missing)
    failed = FAILED_COUNT.search(output)
    summary = f"{failed.group(1)} of {failed.group(2)} properties failed" if failed else ""
    if "VERIFICATION SUCCESSFUL" in output:
        verdict = "passed"
    elif "VERIFICATION FAILED" in output:
        verdict = "failed"
    else:
        return "error", f"cbmc exited {code} without a verdict"
    expected = harness.get("expect", "passed")
    if verdict != expected:
        return ("failed" if verdict == "failed" else "unexpected-pass"), summary or verdict
    if verdict == "failed":
        found = sorted(set(FAILED_PROPERTY.findall(output)))
        wanted = sorted(harness.get("failures", found))
        if found != wanted:
            return "failed", f"failed {', '.join(found)}; expected {', '.join(wanted)}"
        return "expected-failure", f"{summary}: {', '.join(found)}"
    return "passed", summary or verdict


def instrument_steps(harness):
    steps = []
    bodies = harness.get("nondet_bodies", [])
    if bodies:
        removed = [part for name in bodies for part in ("--remove-function-body", name)]
        pattern = "^(" + "|".join(bodies) + ")$"
        steps.append(("nondet", [*removed, "--generate-function-body", pattern,
                                 "--generate-function-body-options", "nondet-return"]))
    if harness.get("uninitialized"):
        steps.append(("uninitialized", ["--uninitialized-check"]))
    return steps


def run_harness(name, manifest, work, env, runner=run_tool, cover=False):
    harness = manifest["cbmc"]["harnesses"][name]
    folder = work / "cbmc" / name
    folder.mkdir(parents=True, exist_ok=True)
    program = folder / f"{name}.goto"
    log = folder / f"{name}.log"
    started = time.monotonic()
    command = cbmc_compile_command(harness, program)
    code, output = runner(command, folder, env, harness.get("timeout", 900))
    transcript = ["$ " + " ".join(map(str, command)), output]
    if code != 0:
        log.write_text("\n".join(transcript), encoding="utf-8")
        return Outcome(name, "cbmc", "error", time.monotonic() - started, "goto-cc failed", log)
    steps = [step for step in instrument_steps(harness) if not (cover and step[0] == "uninitialized")]
    for label, options in steps:
        changed = folder / f"{name}-{label}.goto"
        command = ["goto-instrument", *options, str(program), str(changed)]
        code, output = runner(command, folder, env, 300)
        transcript += ["$ " + " ".join(command), output]
        if code != 0:
            log.write_text("\n".join(transcript), encoding="utf-8")
            return Outcome(name, "cbmc", "error", time.monotonic() - started, "goto-instrument failed", log)
        program = changed
    command = cbmc_cover_command(harness, program) if cover else cbmc_check_command(manifest, harness, program)
    left = max(1, harness.get("timeout", 900) - int(time.monotonic() - started))
    code, output = runner(command, folder, env, left)
    transcript += ["$ " + " ".join(map(str, command)), output]
    log.write_text("\n".join(transcript), encoding="utf-8")
    if cover:
        status = "timeout" if code is None else "covered"
        return Outcome(name, "cbmc", status, time.monotonic() - started,
                       coverage_text(coverage_by_file(output, harness)) or "nothing reached", log)
    status, detail = judge_cbmc(code, output, harness)
    return Outcome(name, "cbmc", status, time.monotonic() - started, detail, log)


def judge_tlc(code, output, model):
    if code is None:
        return "timeout", "stopped at the time limit"
    states = TLC_STATES.search(output)
    depth = TLC_DEPTH.search(output)
    counts = f"{states.group(1)} states, {states.group(2)} distinct" if states else ""
    if depth:
        counts += f", depth {depth.group(1)}"
    violated = re.search(r"Error: (Invariant \S+ is violated|Temporal properties were violated|Deadlock reached"
                         r"|.*property.*violated.*)", output)
    if "Model checking completed. No error has been found." in output:
        verdict = "passed"
    elif violated:
        verdict = "violated"
    else:
        return "error", f"tlc exited {code} without a verdict"
    detail = counts if verdict == "passed" else f"{violated.group(1)}; {counts}".strip("; ")
    expected = model.get("expect", "passed")
    if verdict != expected:
        return ("failed" if verdict == "violated" else "unexpected-pass"), detail
    if verdict == "violated":
        wanted = model.get("property", "")
        named = violated.group(1)
        matches = (wanted == "temporal" and named.startswith("Temporal")) or f"Invariant {wanted} " in named
        if not matches:
            return "failed", f"{detail}; expected {wanted or 'a named property'}"
        return "expected-violation", detail
    return "passed", detail


def tlc_command(model, folder, jar):
    java = ["java", f"-Djava.io.tmpdir={folder / 'tmp'}", "-XX:+UseParallelGC", "-cp", str(jar), "tlc2.TLC"]
    return java + ["-config", model["config"], "-workers", str(model.get("workers", 1)), "-metadir",
                   str(folder / "states"), "-cleanup", model["spec"]]


def run_model(name, manifest, work, env, runner=run_tool, jar=None):
    model = manifest["tla"]["models"][name]
    folder = work / "tla" / name
    (folder / "tmp").mkdir(parents=True, exist_ok=True)
    for file_name in {model["spec"], model["config"], *model.get("extra", [])}:
        shutil.copyfile(FORMAL / "tla" / file_name, folder / file_name)
    log = folder / f"{name}.log"
    started = time.monotonic()
    command = tlc_command(model, folder, TLA_JAR if jar is None else jar)
    code, output = runner(command, folder, env, model.get("timeout", 900))
    log.write_text("$ " + " ".join(map(str, command)) + "\n" + output, encoding="utf-8")
    status, detail = judge_tlc(code, output, model)
    return Outcome(name, "tla", status, time.monotonic() - started, detail, log)


def selected(manifest, kinds, harness, everything=False):
    chosen = []
    for kind, table in (("cbmc", manifest["cbmc"]["harnesses"]), ("tla", manifest["tla"]["models"])):
        if kind in kinds:
            chosen += [(kind, name) for name, entry in table.items()
                       if name == harness or (harness is None and (everything or entry.get("default", True)))]
    if harness is not None and not chosen:
        known = ", ".join([*manifest["cbmc"]["harnesses"], *manifest["tla"]["models"]])
        raise SystemExit(f"Unknown harness or model {harness!r}; known: {known}")
    return chosen


def tool_problems(manifest, kinds, which=shutil.which, tla_check=tla_problem):
    problems = []
    if "cbmc" in kinds and missing_cbmc(which):
        problems.append(cbmc_install_steps())
    if "tla" in kinds:
        problem = tla_check(manifest["tla"], which)
        if problem is not None:
            problems.append(f"{problem}.\n{tla_install_steps(manifest['tla'])}")
    return problems


def work_directory(stamp=None):
    stamp = stamp or time.strftime("%Y%m%d-%H%M%S")
    base = SCRATCH / "reports" / "formal" / BUILD_NAME
    folder = base / stamp
    suffix = 1
    while folder.exists():
        suffix += 1
        folder = base / f"{stamp}-{suffix}"
    return folder


def environment(work):
    env = dict(os.environ)
    temporary = SCRATCH / "tmp" / "formal" / BUILD_NAME
    temporary.mkdir(parents=True, exist_ok=True)
    env["TMPDIR"] = str(temporary)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    return env


def write_summary(work, outcomes):
    rows = [outcome.as_dict() for outcome in outcomes]
    (work / "summary.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    lines = ["| Check | Kind | Result | Wall seconds | CPU seconds | Detail |", "|---|---|---|---|---|---|"]
    lines += [f"| {row['name']} | {row['kind']} | {row['status']} | {row['seconds']} | {row['cpu_seconds']} "
              f"| {row['detail']} |" for row in rows]
    (work / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def good(outcome):
    return outcome.status in ("passed", "expected-failure", "expected-violation", "covered")


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    kind = parser.add_mutually_exclusive_group()
    kind.add_argument("--cbmc", action="store_true", help="run only the CBMC harnesses")
    kind.add_argument("--tla", action="store_true", help="run only the TLA+ models")
    parser.add_argument("--harness", help="run one harness or model by name")
    parser.add_argument("--list", action="store_true", help="list the harnesses and models without running them")
    parser.add_argument("--all", action="store_true",
                        help="also run the opt-in harnesses that do not finish within the usual time budget")
    parser.add_argument("--fetch-tla", action="store_true", help="download and verify the pinned tla2tools.jar")
    parser.add_argument("--cover", action="store_true",
                        help="CBMC only: report the blocks each harness reaches in its sources instead of checking")
    return parser.parse_args(argv)


def kinds_of(args):
    if args.cbmc:
        return ("cbmc",)
    if args.tla:
        return ("tla",)
    return ("cbmc", "tla")


def require_scratch():
    if not os.path.ismount("/Volumes/Scratch"):
        raise SystemExit("Scratch is not mounted; no formal output was written")


def main(argv=None, runner=run_tool, which=shutil.which, tla_check=tla_problem):
    args = arguments(argv)
    manifest = load_manifest()
    chosen = selected(manifest, ("cbmc",) if args.cover else kinds_of(args), args.harness, args.all)
    if args.list:
        for kind, name in chosen:
            print(f"{kind}\t{name}")
        return 0
    require_scratch()
    if args.fetch_tla:
        print(f"Saved {fetch_tla(manifest['tla'])}")
        return 0
    problems = tool_problems(manifest, {kind for kind, _ in chosen}, which, tla_check)
    if problems:
        print("\n\n".join(problems), file=sys.stderr)
        return EXIT_MISSING_TOOLS
    work = work_directory()
    work.mkdir(parents=True, exist_ok=True)
    env = environment(work)
    outcomes = []
    for kind, name in chosen:
        print(f"{kind} {name} ...", flush=True)
        before = child_cpu_seconds()
        if kind == "cbmc":
            outcome = run_harness(name, manifest, work, env, runner, cover=args.cover)
        else:
            outcome = run_model(name, manifest, work, env, runner)
        outcome.cpu = child_cpu_seconds() - before
        print(f"  {outcome.status} in {outcome.seconds:.0f} s ({outcome.cpu:.0f} s CPU): {outcome.detail}",
              flush=True)
        outcomes.append(outcome)
    write_summary(work, outcomes)
    print(f"Results: {work}")
    return 0 if all(good(outcome) for outcome in outcomes) else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
