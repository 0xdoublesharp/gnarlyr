#!/usr/bin/env python3
"""Run the on-demand formal checks: CBMC harnesses for the card-file readers and TLC on the TLA+ models."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
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
COVER_GOAL = re.compile(r"^\[\S+\.coverage\.\d+\] file (\S+) line \d+ function (\S+) block .*: (SATISFIED|FAILED)$",
                        re.MULTILINE)
FAILED_LINE = re.compile(r"^\[([^\].]+)\.([a-z_]+)\.\d+\] line (\d+) .*: FAILURE$", re.MULTILINE)
CONTRACT_MACRO = re.compile(r"^#define FORMAL_CONTRACT_(\w+)\b", re.MULTILINE)
LOOP_MACRO = re.compile(r"^#define FORMAL_LOOP_(\w+)_(\d+)\b", re.MULTILINE)
LOOP_KEYWORD = re.compile(r"\b(for|while)\s*\(")
MEMORY_CLASSES = ("array_bounds", "pointer_dereference", "pointer_arithmetic", "pointer", "overflow",
                  "precondition_instance", "no_body", "assigns")
DFCC_PREFIX = "__CPROVER_contracts_original_"
CPU = threading.local()
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
        self.coverage = None

    def as_dict(self):
        cpu = None if self.cpu is None else round(self.cpu, 1)
        return {"name": self.name, "kind": self.kind, "status": self.status,
                "seconds": round(self.seconds, 1), "cpu_seconds": cpu, "detail": self.detail,
                "coverage": self.coverage, "log": str(self.log)}


def cpu_seconds():
    return getattr(CPU, "seconds", 0.0)


def charge_cpu(seconds):
    CPU.seconds = cpu_seconds() + seconds


def run_tool(command, cwd, env, timeout):
    process = subprocess.Popen([str(part) for part in command], cwd=cwd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, start_new_session=True)
    chunks = []
    reader = threading.Thread(target=lambda: chunks.append(process.stdout.read()), daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    timed_out = False
    while True:
        pid, status, usage = os.wait4(process.pid, os.WNOHANG)
        if pid:
            break
        if time.monotonic() > deadline:
            timed_out = True
            os.killpg(process.pid, signal.SIGKILL)
            pid, status, usage = os.wait4(process.pid, 0)
            break
        time.sleep(0.2)
    process.returncode = os.waitstatus_to_exitcode(status)
    reader.join()
    charge_cpu(usage.ru_utime + usage.ru_stime)
    output = "".join(chunks)
    return (None if timed_out else process.returncode), output


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


def contract_headers(harness):
    return [FORMAL / "cbmc" / "contracts" / name for name in harness.get("contracts", [])]


def contract_macros(headers):
    contracts, loops = {}, {}
    for header in headers:
        text = Path(header).read_text(encoding="utf-8")
        for name in CONTRACT_MACRO.findall(text):
            contracts[name] = f"FORMAL_CONTRACT_{name}"
        for name, index in LOOP_MACRO.findall(text):
            loops.setdefault(name, {})[int(index)] = f"FORMAL_LOOP_{name}_{index}"
    return contracts, loops


def closing(text, start, opening="(", closer=")"):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closer:
            depth -= 1
            if depth == 0:
                return index
    raise SystemExit(f"unbalanced {opening}{closer} from offset {start}")


def function_definitions(text, name):
    spans = []
    for match in re.finditer(rf"^[A-Za-z_][\w \*]*?\b{re.escape(name)}\(", text, re.MULTILINE):
        close = closing(text, match.end() - 1)
        body = close + 1
        while body < len(text) and text[body] in " \t\n":
            body += 1
        if body < len(text) and text[body] == "{":
            spans.append((close, body, closing(text, body, "{", "}")))
    return spans


def defined_functions(text):
    names = re.findall(r"^[A-Za-z_][\w \*]*?\b(\w+)\([^;{]*?\)\s*\{", text, re.MULTILINE | re.DOTALL)
    return [name for name in names if name not in ("if", "for", "while", "switch")]


def loop_heads(text, start, end):
    heads = []
    for match in LOOP_KEYWORD.finditer(text, start, end):
        close = closing(text, match.end() - 1)
        after = close + 1
        while after < end and text[after] in " \t\n":
            after += 1
        if text[after] != ";":
            heads.append(close)
    return heads


def plant_text(text, plant, label):
    if text.count(plant["find"]) != 1:
        raise SystemExit(f"{label}: the planted change must match exactly once")
    if "\n" in plant["find"] or "\n" in plant["replace"]:
        raise SystemExit(f"{label}: a planted change stays on one line")
    return text.replace(plant["find"], plant["replace"])


def patch_text(text, contracts, loops, label):
    inserts, applied = [], []
    for name in sorted(set(contracts) | set(loops)):
        spans = function_definitions(text, name)
        if not spans:
            continue
        if len(spans) != 1:
            raise SystemExit(f"{label}: {name} is defined {len(spans)} times")
        close, body, end = spans[0]
        if name in contracts:
            inserts.append((close + 1, " " + contracts[name]))
            applied.append(contracts[name])
        heads = loop_heads(text, body, end)
        if name in loops and sorted(loops[name]) != list(range(len(heads))):
            raise SystemExit(f"{label}: {name} has {len(heads)} loops; give each one a contract or none")
        for index, macro in sorted(loops.get(name, {}).items()):
            if index >= len(heads):
                raise SystemExit(f"{label}: {macro} names loop {index}, but {name} has {len(heads)} loops")
            inserts.append((heads[index] + 1, " " + macro))
            applied.append(macro)
    for position, insert in sorted(inserts, reverse=True):
        text = text[:position] + insert + text[position:]
    return text, applied


def patched_sources(harness, folder, source=SOURCE):
    contracts, loops = contract_macros(contract_headers(harness))
    plant = harness.get("plant")
    names = list(dict.fromkeys([*harness.get("patch", []), *([plant["file"]] if plant else [])]))
    patched, applied = {}, []
    for name in names:
        text = (source / name).read_text(encoding="utf-8")
        if plant and plant["file"] == name:
            text = plant_text(text, plant, name)
        text, done = patch_text(text, contracts, loops, name)
        target = folder / "patched" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        patched[name] = target
        applied += done
    wanted = [*([harness["enforce"]] if harness.get("enforce") else []), *harness.get("replace", [])]
    missing = [name for name in wanted if "/" not in name and f"FORMAL_CONTRACT_{name}" not in applied]
    if missing:
        raise SystemExit(f"no patched source carries the contract of {', '.join(missing)}")
    return patched, applied


def cbmc_compile_command(harness, output, source=SOURCE, patched=None):
    patched = patched or {}
    stubs = FORMAL / "cbmc" / "stubs"
    files = [patched.get(name, source / name) for name in harness["sources"]]
    files += [stubs / name for name in harness.get("stubs", [])]
    folder = "units" if harness.get("unit") else "harnesses"
    files.append(FORMAL / "cbmc" / folder / f"{harness['entry']}.c")
    defines = [f"-D{key}={value}" for key, value in sorted(harness.get("defines", {}).items())]
    includes = [f"-I{folder}" for folder in dict.fromkeys(path.parent for path in patched.values())]
    includes += [f"-I{source / folder}" for folder in harness.get("includes", [])]
    includes += [f"-I{stubs}"] + include_directories(source)
    for header in harness.get("preinclude", []):
        includes += ["-include", str(stubs / header)]
    for header in contract_headers(harness):
        includes += ["-include", str(header)]
    entry = ["--function", "harness"] if harness.get("unit") else []
    return ["goto-cc", "-m32", "-funsigned-char", "-ffreestanding", "-std=c11", *entry, *firmware_definitions(),
            *defines, *includes, *map(str, files), "-o", str(output)]


def entry_options(harness):
    return [] if harness.get("unit") else ["--function", "harness"]


UNIT_LIBRARY_UNWIND = 34


def unwind_options(harness):
    if harness.get("unit"):
        return ["--unwind", str(UNIT_LIBRARY_UNWIND), "--object-bits", str(harness.get("object_bits", 11))]
    options = ["--unwind", str(harness["unwind"])]
    if harness.get("unwindset"):
        options += ["--unwindset", harness["unwindset"]]
    return options


def cbmc_cover_command(harness, program):
    checks = [flag for flag in harness.get("checks", []) if flag != "--unsigned-overflow-check"]
    return ["cbmc", str(program), *entry_options(harness), "--cover", "location", *unwind_options(harness), *checks]


def coverage_by_file(output, harness, source=SOURCE):
    wanted = {str(source / name): name for name in harness["sources"]}
    counts = {}
    for path, _, verdict in COVER_GOAL.findall(output):
        if path in wanted:
            reached, total = counts.get(wanted[path], (0, 0))
            counts[wanted[path]] = (reached + (verdict == "SATISFIED"), total + 1)
    return counts


def coverage_by_function(output, targets):
    counts = {}
    for _, function, verdict in COVER_GOAL.findall(output):
        name = function[len(DFCC_PREFIX):] if function.startswith(DFCC_PREFIX) else function
        if name in targets:
            reached, total = counts.get(name, (0, 0))
            counts[name] = (reached + (verdict == "SATISFIED"), total + 1)
    return counts


def coverage_text(counts):
    return ", ".join(f"{Path(name).name} {reached}/{total}" for name, (reached, total) in sorted(counts.items())
                     if reached)


def coverage_fraction(counts):
    reached = sum(done for done, _ in counts.values())
    total = sum(total for _, total in counts.values())
    return reached, total


def cbmc_check_command(manifest, harness, program):
    command = ["cbmc", str(program), *entry_options(harness), *manifest["cbmc"]["checks"],
               *harness.get("checks", []), *unwind_options(harness)]
    command.append("--unwinding-assertions" if harness.get("unwinding_assertions", True) else "--no-unwinding-assertions")
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
    if verdict == "failed" and harness.get("planted"):
        return judge_planted(output, harness["planted"], summary)
    if verdict == "failed":
        found = sorted(set(FAILED_PROPERTY.findall(output)))
        wanted = sorted(harness.get("failures", found))
        if found != wanted:
            return "failed", f"failed {', '.join(found)}; expected {', '.join(wanted)}"
        return "expected-failure", f"{summary}: {', '.join(found)}"
    return "passed", summary or verdict


def judge_planted(output, planted, summary):
    hits = [(function, kind, int(line)) for function, kind, line in FAILED_LINE.findall(output)]
    if "property" in planted:
        wanted = [hit for hit in hits if f"{hit[0]}.{hit[1]}".startswith(planted["property"])]
        if not wanted:
            return "failed", f"the planted fault was not caught as {planted['property']}"
        return "expected-failure", f"{summary}: caught {wanted[0][0]}.{wanted[0][1]} at line {wanted[0][2]}"
    wanted = [hit for hit in hits if hit[0] == planted["function"] and hit[2] == planted["line"]
              and hit[1] in MEMORY_CLASSES]
    if not wanted:
        found = ", ".join(f"{function}.{kind} line {line}" for function, kind, line in hits) or "nothing"
        return "failed", f"the planted fault at {planted['function']} line {planted['line']} was not caught; failed: {found}"
    return "expected-failure", f"{summary}: caught {wanted[0][0]}.{wanted[0][1]} at line {wanted[0][2]}"


SHOW_LOOP = re.compile(r"^Loop (\S+)\.(\d+):", re.MULTILINE)


def contract_loops(applied):
    functions = set()
    for macro in applied:
        match = re.fullmatch(r"FORMAL_LOOP_(\w+)_(\d+)", macro)
        if match:
            functions.add(match.group(1))
    return functions


def plain_unwindset(output, applied, harness):
    keep = contract_loops(applied)
    chosen = dict(part.rsplit(":", 1) for part in harness.get("unwindset", "").split(",") if part)
    loops = [(name, f"{name}.{index}") for name, index in SHOW_LOOP.findall(output)]
    return ",".join(f"{loop}:{chosen.get(loop, harness['unwind'])}" for name, loop in loops
                    if name not in keep and not name.startswith("__CPROVER"))


def slim_steps(harness):
    replaced = sorted({name.split("/")[0] for name in harness.get("replace", [])} | set(harness.get("bodiless", [])))
    steps = [("bodiless", [part for name in replaced for part in ("--remove-function-body", name)])] if replaced else []
    restricted = [part for target in harness.get("restrict_function_pointers", [])
                  for part in ("--restrict-function-pointer", target)]
    return [("direct", [*restricted, "--remove-function-pointers"]), *steps, ("used", ["--drop-unused-functions"])]


def dfcc_options(harness, applied):
    options = []
    if harness.get("unit") and harness.get("dfcc", True):
        options += ["--dfcc", "harness"]
        if harness.get("enforce"):
            options += ["--enforce-contract", harness["enforce"]]
        for name in harness.get("replace", []):
            options += ["--replace-call-with-contract", name]
    if any(name.startswith("FORMAL_LOOP_") for name in applied):
        options.append("--apply-loop-contracts")
    return options


def instrument_steps(harness, applied=()):
    steps = []
    bodies = harness.get("nondet_bodies", [])
    if bodies:
        removed = [part for name in bodies for part in ("--remove-function-body", name)]
        pattern = "^(" + "|".join(bodies) + ")$"
        steps.append(("nondet", [*removed, "--generate-function-body", pattern,
                                 "--generate-function-body-options", "nondet-return"]))
    contracts = dfcc_options(harness, applied)
    if contracts:
        steps.append(("contracts", contracts))
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
    try:
        patched, applied = patched_sources(harness, folder)
    except SystemExit as error:
        log.write_text(str(error), encoding="utf-8")
        return Outcome(name, "cbmc", "error", time.monotonic() - started, str(error), log)
    command = cbmc_compile_command(harness, program, patched=patched)
    code, output = runner(command, folder, env, harness.get("timeout", 900))
    transcript = ["$ " + " ".join(map(str, command)), output]
    if code != 0:
        log.write_text("\n".join(transcript), encoding="utf-8")
        return Outcome(name, "cbmc", "error", time.monotonic() - started, "goto-cc failed", log)
    steps = [step for step in instrument_steps(harness, applied) if not (cover and step[0] == "uninitialized")]
    if harness.get("unit"):
        for label, options in slim_steps(harness):
            slim = folder / f"{name}-{label}.goto"
            command = ["goto-instrument", *options, str(program), str(slim)]
            code, output = runner(command, folder, env, 300)
            transcript += ["$ " + " ".join(command), output]
            if code != 0:
                log.write_text("\n".join(transcript), encoding="utf-8")
                return Outcome(name, "cbmc", "error", time.monotonic() - started, "goto-instrument failed", log)
            program = slim
        code, output = runner(["goto-instrument", "--show-loops", str(program)], folder, env, 300)
        unwindset = plain_unwindset(output, applied, harness)
        if unwindset:
            steps.insert(len(steps) - 1, ("unwound", ["--unwindset", unwindset, "--unwinding-assertions"]))
    for label, options in steps:
        changed = folder / f"{name}-{label}.goto"
        command = ["goto-instrument", *options, str(program), str(changed)]
        left = max(1, harness.get("timeout", 900) - int(time.monotonic() - started))
        code, output = runner(command, folder, env, left)
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
        return cover_outcome(name, harness, code, output, time.monotonic() - started, log)
    status, detail = judge_cbmc(code, output, harness)
    return Outcome(name, "cbmc", status, time.monotonic() - started, detail, log)


def cover_outcome(name, harness, code, output, seconds, log):
    if code is None:
        return Outcome(name, "cbmc", "timeout", seconds, "stopped at the time limit", log)
    if harness.get("targets"):
        counts = coverage_by_function(output, harness["targets"])
        reached, total = coverage_fraction(counts)
        missing = sorted(set(harness["targets"]) - set(counts))
        detail = f"{reached}/{total} blocks of the target functions"
        detail += f" ({', '.join(f'{n} {r}/{t}' for n, (r, t) in sorted(counts.items()))})"
        if missing:
            detail += f"; no blocks found for {', '.join(missing)}"
        outcome = Outcome(name, "cbmc", "covered", seconds, detail, log)
        outcome.coverage = [reached, total]
        return outcome
    return Outcome(name, "cbmc", "covered", seconds,
                   coverage_text(coverage_by_file(output, harness)) or "nothing reached", log)


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


def harness_patterns(harness):
    if harness is None:
        return None
    values = [harness] if isinstance(harness, str) else harness
    return [part for value in values for part in value.split(",") if part]


def selected(manifest, kinds, harness, everything=False):
    patterns = harness_patterns(harness)
    chosen = []
    for kind, table in (("cbmc", manifest["cbmc"]["harnesses"]), ("tla", manifest["tla"]["models"])):
        if kind in kinds:
            chosen += [(kind, name) for name, entry in table.items()
                       if (patterns is not None and any(fnmatch.fnmatchcase(name, p) for p in patterns))
                       or (patterns is None and (everything or entry.get("default", True)))]
    if patterns is not None and not chosen:
        known = ", ".join([*manifest["cbmc"]["harnesses"], *manifest["tla"]["models"]])
        raise SystemExit(f"Unknown harness or model {', '.join(patterns)!r}; known: {known}")
    return chosen


def enforced_by(manifest):
    owners = {}
    for name, harness in manifest["cbmc"]["harnesses"].items():
        proven = harness.get("enforce") or harness.get("proves")
        if proven and not harness.get("plant"):
            owners.setdefault(proven, []).append(name)
    return owners


def closure_problems(manifest, source=SOURCE):
    problems = []
    owners = enforced_by(manifest)
    axioms = set(manifest["cbmc"].get("axioms", []))
    harnesses = manifest["cbmc"]["harnesses"]
    for name, harness in harnesses.items():
        for replaced in harness.get("replace", []):
            if replaced not in owners and replaced not in axioms:
                problems.append(f"{name}: replaces {replaced}, which no harness enforces")
    checked = {target for harness in harnesses.values() if not harness.get("plant")
               for target in harness.get("targets", [])}
    for family, entry in manifest["cbmc"].get("families", {}).items():
        for path in entry["files"]:
            text = (source / path).read_text(encoding="utf-8")
            for function in defined_functions(text):
                excused = entry.get("outside", []) + entry.get("pending", [])
                if function not in checked and function not in excused:
                    problems.append(f"{family}: {path} {function} has no harness")
    return problems


def closure_pending(manifest):
    return [f"{family}: {function} awaits a harness for the current code"
            for family, entry in manifest["cbmc"].get("families", {}).items()
            for function in entry.get("pending", [])]


def dependency_notes(manifest, outcomes):
    status = {outcome.name: outcome.status for outcome in outcomes}
    owners = enforced_by(manifest)
    for outcome in outcomes:
        harness = manifest["cbmc"]["harnesses"].get(outcome.name) if outcome.kind == "cbmc" else None
        if not harness or outcome.status != "passed":
            continue
        open_contracts = [name for name in harness.get("replace", [])
                          if name in owners and not any(status.get(owner) == "passed" for owner in owners[name])]
        if open_contracts:
            outcome.detail += f"; assumes {', '.join(open_contracts)}, not proven in this run"


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
    base = Path(os.environ.get("FORMAL_REPORTS") or SCRATCH / "reports" / "formal" / BUILD_NAME)
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
    return outcome.status in ("passed", "expected-failure", "expected-violation", "covered", "reproduced", "fixed")


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    kind = parser.add_mutually_exclusive_group()
    kind.add_argument("--cbmc", action="store_true", help="run only the CBMC harnesses")
    kind.add_argument("--tla", action="store_true", help="run only the TLA+ models")
    parser.add_argument("--harness", action="append",
                        help="run harnesses or models by name or shell pattern; repeat or separate with commas")
    parser.add_argument("--list", action="store_true", help="list the harnesses and models without running them")
    parser.add_argument("--all", action="store_true",
                        help="also run the opt-in harnesses that do not finish within the usual time budget")
    parser.add_argument("--fetch-tla", action="store_true", help="download and verify the pinned tla2tools.jar")
    parser.add_argument("--cover", action="store_true",
                        help="CBMC only: report the blocks each harness reaches in its sources instead of checking")
    parser.add_argument("--jobs", type=int, default=1, help="checks to run at once (each checker is single-threaded)")
    parser.add_argument("--repro", action="store_true",
                        help="build and run the host programs that replay CBMC counterexamples on the real code")
    parser.add_argument("--closure", action="store_true",
                        help="list replaced contracts that no harness proves and reader functions with no harness")
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


def repro_command(entry, output, source=SOURCE):
    files = [FORMAL / "repro" / entry["entry"]] + [source / name for name in entry["sources"]]
    includes = [f"-I{source / folder}" for folder in entry.get("includes", [])] + include_directories(source)
    return ["cc", "-std=c11", "-O1", "-funsigned-char", *firmware_definitions(), *includes, *map(str, files),
            "-o", str(output)]


def run_repro(name, manifest, work, env, runner=run_tool):
    entry = manifest["repro"][name]
    folder = work / "repro" / name
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / f"{name}.log"
    started = time.monotonic()
    command = repro_command(entry, folder / name)
    code, output = runner(command, folder, env, 300)
    transcript = ["$ " + " ".join(map(str, command)), output]
    if code == 0:
        code, output = runner([str(folder / name)], folder, env, entry.get("timeout", 300))
        transcript += [f"$ {folder / name}", output]
        status = "reproduced" if code == 0 else "not-reproduced"
        if entry.get("expect") == "fixed":
            status = "regressed" if code == 0 else "fixed"
    else:
        status = "error"
    log.write_text("\n".join(transcript), encoding="utf-8")
    detail = output.strip().splitlines()[-1] if output.strip() else status
    return Outcome(name, "repro", status, time.monotonic() - started, detail, log)


def run_check(kind, name, manifest, work, env, runner, cover):
    before = cpu_seconds()
    if kind == "cbmc":
        outcome = run_harness(name, manifest, work, env, runner, cover=cover)
    elif kind == "repro":
        outcome = run_repro(name, manifest, work, env, runner)
    else:
        outcome = run_model(name, manifest, work, env, runner)
    outcome.cpu = cpu_seconds() - before
    return outcome


def record_ledger(work, outcomes, cover):
    host = socket.gethostname()
    ledger = work.parent / f"ledger-{host.split('.')[0]}.jsonl"
    with open(ledger, "a", encoding="utf-8") as stream:
        for outcome in outcomes:
            row = {**outcome.as_dict(), "host": host, "mode": "cover" if cover else "check", "run": work.name}
            stream.write(json.dumps(row) + "\n")


def run_all(chosen, manifest, work, env, runner, args):
    outcomes = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(run_check, kind, name, manifest, work, env, runner, args.cover): name
                   for kind, name in chosen}
        for future in as_completed(futures):
            outcome = future.result()
            print(f"{outcome.kind} {outcome.name}: {outcome.status} in {outcome.seconds:.0f} s "
                  f"({outcome.cpu:.0f} s CPU): {outcome.detail}", flush=True)
            outcomes.append(outcome)
    order = {name: index for index, (_, name) in enumerate(chosen)}
    return sorted(outcomes, key=lambda outcome: order[outcome.name])


def main(argv=None, runner=run_tool, which=shutil.which, tla_check=tla_problem):
    args = arguments(argv)
    manifest = load_manifest()
    if args.closure:
        problems = closure_problems(manifest)
        pending = closure_pending(manifest)
        print("\n".join(problems) or "every replaced contract has a harness and every reader function is checked "
              "or listed")
        if pending:
            print(f"{len(pending)} functions without a harness yet:\n" + "\n".join(pending))
        return EXIT_FAILED if problems else 0
    if args.repro:
        chosen = [("repro", name) for name in manifest.get("repro", {})
                  if args.harness is None or any(fnmatch.fnmatchcase(name, p) for p in harness_patterns(args.harness))]
    else:
        chosen = selected(manifest, ("cbmc",) if args.cover else kinds_of(args), args.harness, args.all)
    if args.list:
        for kind, name in chosen:
            print(f"{kind}\t{name}")
        return 0
    require_scratch()
    if args.fetch_tla:
        print(f"Saved {fetch_tla(manifest['tla'])}")
        return 0
    problems = tool_problems(manifest, {kind for kind, _ in chosen} - {"repro"}, which, tla_check)
    if problems:
        print("\n\n".join(problems), file=sys.stderr)
        return EXIT_MISSING_TOOLS
    work = work_directory()
    work.mkdir(parents=True, exist_ok=True)
    env = environment(work)
    outcomes = run_all(chosen, manifest, work, env, runner, args)
    if not args.cover:
        dependency_notes(manifest, outcomes)
    write_summary(work, outcomes)
    record_ledger(work, outcomes, args.cover)
    print(f"Results: {work}")
    return 0 if all(good(outcome) for outcome in outcomes) else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
