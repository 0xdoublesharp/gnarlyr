"""Opt-in static analysis: clang-tidy, the Clang Static Analyzer and a worst-case stack bound for the device build."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys

ANALYSED_PREFIXES = ("src/", "platform/sim/", "platform/device/logic/")
SKIPPED_PREFIXES = ("third_party/", "tests/", "build/", "src/fonts/")
DIAGNOSTIC = re.compile(r"^(/[^:]+):(\d+):(\d+): (warning|error): (.*?) \[([\w.,-]+)\]$")
DROPPED_FLAGS = re.compile(r"^-(fsanitize|fno-sanitize|fno-omit-frame-pointer|Werror|fcolor-diagnostics|MD$|MT$|MF$)")
ANALYZER_CHECKERS = (
    "core", "unix", "deadcode", "security.ArrayBound", "core.BitwiseShift", "optin.core.EnumCastOutOfRange",
    "optin.portability.UnixAPI", "unix.cstring.NotNullTerminated", "unix.cstring.UninitializedRead",
    "optin.taint.TaintedDiv", "alpha.core.BoolAssignment",
    "alpha.core.TestAfterDivZero", "alpha.security.ReturnPtrRange", "alpha.unix.cstring.OutOfBounds",
    "alpha.unix.cstring.BufferOverlap",
)
ANALYZER_DISABLED = ("security.insecureAPI.DeprecatedOrUnsafeBufferHandling", "unix.Malloc")
CI_NODE = re.compile(r'^node: \{ title: "([^"]+)" label: "([^"]*)"(.*)\}$')
CI_EDGE = re.compile(r'^edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"(?: label: "([^"]*)")? \}$')
CI_BYTES = re.compile(r"\\n(\d+) bytes \((static|dynamic|dynamic,bounded)\)")
CI_DYNAMIC = re.compile(r"\\n(\d+) dynamic objects")
CI_LOCATION = re.compile(r"^[^\\]+\\n([^\\]+)")
INDIRECT = "__indirect_call"
EXCEPTION_FRAME = 32
CORE0_STACK = 16384
CORE0_MEASURED = 8100
CORE0_ENTRIES = {"main": "device_reset", "systick": "board_systick_handler", "fault": "device_fault_handler"}
ENTRY_STUBS = {"device_systick_entry", "device_fault_entry", "device_default_handler", "core1_default_handler"}
CORE1_ENTRIES = {"main": "core1_reset", "systick": "core1_tick"}
CORE1_STACK_TOP = 0x0102E000
CORE1_RESERVES = {"echo_core1": 4096, "echo_core1_mp3": 12288, "echo_core1_m4a": 24576,
                  "echo_core1_vorbis": 16384, "echo_core1_opus": 16384, "echo_core1_wma": 12288,
                  "echo_core1_dsd": 16384, "echo_core1_ape": 16384}


def relative(path, source):
    try:
        return str(Path(path).resolve().relative_to(source.resolve()))
    except ValueError:
        return str(path)


def selected_units(database, source):
    units = []
    seen = set()
    for entry in database:
        path = Path(entry["directory"], entry["file"]).resolve()
        name = relative(path, source)
        if name in seen or not name.startswith(ANALYSED_PREFIXES) or name.startswith(SKIPPED_PREFIXES):
            continue
        seen.add(name)
        units.append({**entry, "path": str(path), "name": name})
    return sorted(units, key=lambda unit: unit["name"])


def parse_tidy(output, source):
    findings = []
    for line in output.splitlines():
        match = DIAGNOSTIC.match(line)
        if not match:
            continue
        path, row, column, level, message, checks = match.groups()
        name = relative(path, source)
        if name.startswith(SKIPPED_PREFIXES[:1]):
            continue
        for check in checks.split(","):
            findings.append({"tool": "clang-tidy", "check": check, "path": name, "line": int(row),
                             "column": int(column), "level": level, "message": message})
    return findings


def unit_arguments(unit):
    if "arguments" in unit:
        arguments = list(unit["arguments"])
    else:
        arguments = shlex.split(unit["command"])
    kept = []
    skip = False
    for argument in arguments[1:]:
        if skip:
            skip = False
            continue
        if argument in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if argument in ("-c", unit["file"], unit["path"]) or DROPPED_FLAGS.match(argument):
            continue
        kept.append(argument)
    return kept


def analyzer_command(clang, unit, output):
    command = [clang, "--analyze", "--analyzer-output", "sarif", "-o", str(output)]
    for checker in ANALYZER_CHECKERS:
        command += ["-Xclang", "-analyzer-checker=" + checker]
    for checker in ANALYZER_DISABLED:
        command += ["-Xclang", "-analyzer-disable-checker=" + checker]
    return command + ["-Xclang", "-analyzer-config", "-Xclang", "aggressive-binary-operation-simplification=true",
                      *unit_arguments(unit), unit["path"]]


def parse_sarif(document, source):
    findings = []
    for run in document.get("runs", []):
        for result in run.get("results", []):
            location = (result.get("locations") or [{}])[0].get("physicalLocation", {})
            uri = location.get("artifactLocation", {}).get("uri", "")
            path = uri[len("file://"):] if uri.startswith("file://") else uri
            name = relative(path, source)
            if name.startswith(SKIPPED_PREFIXES[:1]):
                continue
            region = location.get("region", {})
            findings.append({"tool": "clang-analyzer", "check": result.get("ruleId", "?"), "path": name,
                             "line": region.get("startLine", 0), "column": region.get("startColumn", 0),
                             "level": result.get("level", "warning"),
                             "message": result.get("message", {}).get("text", "")})
    return findings


def source_line(source, finding):
    try:
        lines = (source / finding["path"]).read_text(errors="replace").splitlines()
        return lines[finding["line"] - 1].strip() if 0 < finding["line"] <= len(lines) else ""
    except OSError:
        return ""


def fingerprint(finding, text):
    message = re.sub(r"\d+", "N", finding["message"])
    key = "|".join((finding["tool"], finding["check"], finding["path"], message, " ".join(text.split())))
    return hashlib.sha1(key.encode()).hexdigest()[:16]


def deduplicate(findings, source):
    unique = {}
    for finding in findings:
        text = source_line(source, finding)
        finding = {**finding, "fingerprint": fingerprint(finding, text), "text": text}
        unique.setdefault((finding["fingerprint"], finding["line"]), finding)
    return sorted(unique.values(), key=lambda item: (item["path"], item["line"], item["check"]))


def read_baseline(path):
    entries = {}
    if not path.is_file():
        return entries
    for line in path.read_text().splitlines():
        if not line or line.startswith("fingerprint\t"):
            continue
        fields = line.split("\t")
        if len(fields) < 5 or not re.fullmatch(r"[0-9a-f]{16}", fields[0]) or not fields[4].strip():
            raise ValueError(f"baseline entries need fingerprint, tool, check, path and a reason: {line!r}")
        entries[fields[0]] = {"tool": fields[1], "check": fields[2], "path": fields[3], "reason": fields[4]}
    return entries


def compare(findings, baseline, tool=None):
    new = [finding for finding in findings if finding["fingerprint"] not in baseline]
    present = {finding["fingerprint"] for finding in findings}
    stale = sorted(key for key, entry in baseline.items()
                   if key not in present and (tool is None or entry["tool"] == tool))
    return new, stale


def baseline_lines(findings, reasons):
    lines = ["fingerprint\ttool\tcheck\tpath\treason"]
    for finding in sorted(findings, key=lambda item: (item["path"], item["check"], item["fingerprint"])):
        reason = reasons.get(finding["fingerprint"])
        if reason:
            lines.append("\t".join((finding["fingerprint"], finding["tool"], finding["check"], finding["path"],
                                    reason)))
    return lines


def run_units(units, worker, jobs):
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return list(pool.map(worker, units))


def tidy_worker(clang_tidy, build, config, runner):
    def work(unit):
        command = [clang_tidy, "-p", str(build), f"--config-file={config}", "--quiet", unit["path"]]
        code, output = runner(command)
        return {"unit": unit["name"], "code": code, "output": output}
    return work


def analyzer_worker(clang, output_dir, runner):
    def work(unit):
        output = output_dir / (unit["name"].replace("/", "__") + ".sarif")
        code, text = runner(analyzer_command(clang, unit, output))
        return {"unit": unit["name"], "code": code, "output": text, "sarif": output}
    return work


def subprocess_runner(command):
    completed = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
    return completed.returncode, completed.stdout


def function_name(title):
    return title.rsplit(":", 1)[-1]


def parse_callgraph(text, origin):
    nodes = {}
    edges = []
    for line in text.splitlines():
        node = CI_NODE.match(line)
        if node:
            title, label, rest = node.groups()
            title = function_name(title)
            frame = CI_BYTES.search(label)
            dynamic = CI_DYNAMIC.search(label)
            location = CI_LOCATION.match(label)
            nodes[title] = {
                "name": title,
                "frame": int(frame.group(1)) if frame else None,
                "kind": frame.group(2) if frame else ("indirect" if title == INDIRECT else "external"),
                "dynamic_objects": int(dynamic.group(1)) if dynamic else 0,
                "location": location.group(1) if location and frame else "",
                "origin": origin,
            }
            continue
        edge = CI_EDGE.match(line)
        if edge:
            edges.append((function_name(edge.group(1)), function_name(edge.group(2)), edge.group(3) or ""))
    return nodes, edges


class CallGraph:
    def __init__(self):
        self.frames = {}
        self.calls = {}
        self.sites = {}
        self.unknown = set()
        self.dynamic = set()

    def add(self, nodes, edges):
        local = {name for name, node in nodes.items() if node["frame"] is not None}
        for name in local:
            node = nodes[name]
            previous = self.frames.get(name)
            self.frames[name] = max(previous or 0, node["frame"])
            if node["kind"] != "static" or node["dynamic_objects"]:
                self.dynamic.add(name)
            self.calls.setdefault(name, set())
        for source, target, site in edges:
            self.calls.setdefault(source, set()).add(target)
            if target == INDIRECT:
                self.sites.setdefault(source, []).append(site)

    def finish(self):
        for targets in list(self.calls.values()):
            for target in targets:
                if target != INDIRECT and target not in self.frames:
                    self.unknown.add(target)

    def cycles(self):
        index = {}
        low = {}
        stack = []
        on_stack = set()
        found = []
        counter = [0]
        for root in sorted(self.calls):
            if root in index:
                continue
            work = [(root, iter(sorted(self.calls.get(root, ()))))]
            index[root] = low[root] = counter[0]
            counter[0] += 1
            stack.append(root)
            on_stack.add(root)
            while work:
                node, children = work[-1]
                advanced = False
                for child in children:
                    if child == INDIRECT:
                        continue
                    if child not in index:
                        index[child] = low[child] = counter[0]
                        counter[0] += 1
                        stack.append(child)
                        on_stack.add(child)
                        work.append((child, iter(sorted(self.calls.get(child, ())))))
                        advanced = True
                        break
                    if child in on_stack:
                        low[node] = min(low[node], index[child])
                if advanced:
                    continue
                work.pop()
                if work:
                    low[work[-1][0]] = min(low[work[-1][0]], low[node])
                if low[node] == index[node]:
                    component = []
                    while True:
                        member = stack.pop()
                        on_stack.discard(member)
                        component.append(member)
                        if member == node:
                            break
                    if len(component) > 1 or node in self.calls.get(node, ()):
                        found.append(sorted(component))
        return sorted(found)

    def components(self):
        return {member: index for index, component in enumerate(self.cycles()) for member in component}

    def depths(self, indirect_cost=0, indirect_path=()):
        component = self.components()

        def inside(node, child):
            return child == node or (node in component and component.get(child) == component[node])

        memo = {}
        for root in sorted(self.calls):
            if root in memo:
                continue
            work = [root]
            while work:
                node = work[-1]
                if node in memo:
                    work.pop()
                    continue
                pending = [child for child in sorted(self.calls.get(node, ()))
                           if child != INDIRECT and child not in memo and not inside(node, child)]
                if pending:
                    work.extend(pending)
                    continue
                work.pop()
                best = (0, ())
                for child in sorted(self.calls.get(node, ())):
                    if child == INDIRECT:
                        candidate = (indirect_cost, tuple(indirect_path))
                    elif inside(node, child) or child not in memo:
                        continue
                    else:
                        candidate = memo[child]
                    if candidate[0] > best[0]:
                        best = candidate
                memo[node] = (self.frames.get(node, 0) + best[0], (node, *best[1]))
        return memo


def elf_symbol(data, wanted):
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, _ = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + index * shentsize) for index in range(shnum)]
    for header in sections:
        if header[1] == 2:
            strings = sections[header[6]][4]
            for entry in range(header[5] // 16):
                name_offset, value = struct.unpack_from("<II", data, header[4] + entry * 16)
                end = data.index(b"\0", strings + name_offset)
                if data[strings + name_offset:end].decode() == wanted:
                    return value
    return None


def elf_functions(data):
    if data[:4] != b"\x7fELF" or data[4] != 1:
        raise ValueError("not a 32-bit ELF file")
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + index * shentsize) for index in range(shnum)]
    functions = {}
    loaded = []
    for header in sections:
        kind, flags, address, offset, size = header[1], header[2], header[3], header[4], header[5]
        if kind == 2:
            strings = sections[header[6]]
            for entry in range(size // 16):
                name_offset, value, symbol_size, info, _, _ = struct.unpack_from("<IIIBBH", data, offset + entry * 16)
                if info & 0xF == 2 and value:
                    end = data.index(b"\0", strings[4] + name_offset)
                    functions.setdefault(value & ~1, set()).add(data[strings[4] + name_offset:end].decode())
        if kind == 1 and flags & 2:
            loaded.append((address, data[offset:offset + size]))
    return functions, loaded


def address_taken(data):
    functions, loaded = elf_functions(data)
    taken = set()
    for _, contents in loaded:
        for position in range(0, len(contents) - 3, 2):
            word, = struct.unpack_from("<I", contents, position)
            if word & 1 and (word & ~1) in functions:
                taken.update(functions[word & ~1])
    return sorted(taken), sorted(set().union(*functions.values()))


def bound_report(graph, entries, indirect_targets):
    direct = graph.depths()
    excluded = set(entries.values()) | ENTRY_STUBS
    targets = [name for name in indirect_targets if name in direct and name not in excluded]
    worst_target = max(targets, key=lambda name: direct[name][0], default=None)
    one_level = direct[worst_target] if worst_target else (0, ())
    indirect = graph.depths(one_level[0], one_level[1])
    report = {}
    for label, entry in entries.items():
        if entry not in direct:
            report[label] = {"entry": entry, "missing": True}
            continue
        report[label] = {
            "entry": entry,
            "direct_bytes": direct[entry][0],
            "direct_path": [(name, graph.frames.get(name)) for name in direct[entry][1]],
            "indirect_bytes": indirect[entry][0],
            "indirect_path": [(name, graph.frames.get(name)) for name in indirect[entry][1]],
            "indirect_sites_on_path": sum(len(graph.sites.get(name, ())) for name in direct[entry][1]),
        }
    return report, worst_target


def top_paths(graph, count=10):
    depths = graph.depths()
    called = {target for targets in graph.calls.values() for target in targets}
    roots = [name for name in graph.frames if name not in called]
    ranked = sorted(roots, key=lambda name: depths[name][0], reverse=True)[:count]
    return [{"root": name, "bytes": depths[name][0], "path": list(depths[name][1])} for name in ranked]


MAP_FUNCTION = re.compile(r"^ \.(\S+)\s+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+(\S+)$", re.MULTILINE)
MAP_MEMBER = re.compile(r"^(\S+\.a)\(([^)]+\.o(?:bj)?)\)$")


def linked_objects(map_text, build_dir):
    marker = map_text.find("Linker script and memory map")
    map_text = map_text[marker:] if marker >= 0 else map_text
    objects = {}
    for match in MAP_FUNCTION.finditer(map_text):
        function, source = match.groups()
        member = MAP_MEMBER.match(source)
        if member:
            key = (str((build_dir / member.group(1)).resolve()), member.group(2))
        elif re.search(r"\.o(?:bj)?$", source):
            key = str((build_dir / source).resolve())
        else:
            continue
        parts = function.split(".")
        objects.setdefault(key, set()).update(".".join(parts[index:]) for index in range(1, len(parts)))
    return objects


def callgraph_of(object_path):
    return re.sub(r"\.o(?:bj)?$", "", str(object_path)) + ".ci"


def object_of(callgraph_path, lto_objects):
    stem = str(callgraph_path)[:-3]
    return next((stem + suffix for suffix in (".o", ".obj") if stem + suffix in lto_objects), None)


def resolve_aliases(graph, groups):
    for names in groups:
        known = sorted(name for name in names if name in graph.frames)
        if not known:
            continue
        for name in names:
            if name not in graph.frames:
                graph.frames[name] = graph.frames[known[0]]
                graph.calls[name] = set(graph.calls.get(known[0], ()))


def program_graph(program_dir, elf_name, lto_objects):
    graph = CallGraph()
    elf = program_dir / f"{elf_name}.elf"
    ltrans = sorted(program_dir.glob(f"{elf_name}.elf.ltrans*.ci"))
    map_path = program_dir / f"{elf_name}.map"
    members = linked_objects(map_path.read_text(errors="replace"), program_dir) if map_path.is_file() else {}
    files = list(ltrans)
    kept = {}
    for member, functions in members.items():
        if isinstance(member, tuple):
            archive, name = member
            directory = Path(archive).parent / "CMakeFiles" / (Path(archive).stem[3:] + ".dir")
            candidates = list(directory.rglob(Path(callgraph_of(name)).name)) if directory.is_dir() else []
        else:
            candidates = [Path(callgraph_of(member))]
        for candidate in candidates:
            if candidate.is_file() and not (ltrans and object_of(candidate, lto_objects)):
                files.append(candidate)
                kept[candidate] = functions
    for path in files:
        nodes, edges = parse_callgraph(path.read_text(errors="replace"), str(path))
        if path in kept:
            nodes = {name: node for name, node in nodes.items() if node["frame"] is None or name in kept[path]}
            edges = [edge for edge in edges if edge[0] in kept[path]]
        graph.add(nodes, edges)
    if elf.is_file():
        functions, _ = elf_functions(elf.read_bytes())
        keep = set().union(*functions.values()) if functions else set()
        graph.frames = {name: frame for name, frame in graph.frames.items() if name in keep}
        graph.calls = {name: calls for name, calls in graph.calls.items() if name in keep}
        resolve_aliases(graph, functions.values())
    graph.finish()
    return graph, files


def stack_markdown(results):
    lines = ["# Worst-case stack bound", ""]
    for program, result in results.items():
        lines += [f"## {program}", "", f"Reserve required by the linker script or design: {result['budget']} bytes; "
                  f"available in this build: {result['available']} bytes. Call-graph files: {result['files']}.", ""]
        for label, entry in result["entries"].items():
            if entry.get("missing"):
                lines.append(f"- {label} (`{entry['entry']}`): entry not found in the call graph")
                continue
            lines.append(f"- {label} (`{entry['entry']}`): {entry['direct_bytes']} bytes over direct calls; "
                         f"{entry['indirect_bytes']} bytes when each indirect call costs the deepest "
                         f"address-taken function once; {entry['indirect_sites_on_path']} indirect call sites "
                         "on the direct path")
            lines.append("  - path: " + " → ".join(f"{name} ({frame})" for name, frame in entry["indirect_path"]))
        lines += ["", f"Combined ({result['combined_note']}): {result['combined_direct']} bytes over direct calls; "
                  f"{result['combined']} bytes with the one-level indirect estimate.", ""]
        lines += [f"Recursion cycles: {len(result['cycles'])}"] + [
            "- " + " ↔ ".join(component) for component in result["cycles"][:20]]
        lines += ["", f"Functions without a frame size (assembly, libgcc or not compiled here): "
                  f"{len(result['unknown'])}: " + ", ".join(sorted(result["unknown"])[:40]), ""]
        lines += [f"Functions with dynamic or unbounded frames: " + (", ".join(sorted(result["dynamic"])) or "none"),
                  "", f"Indirect call sites: {result['indirect_sites']} in {result['indirect_callers']} functions; "
                  f"address-taken functions: {result['address_taken']}.", ""]
        lines += ["Deepest roots:", ""] + [f"- {item['root']}: {item['bytes']} bytes: " + " → ".join(item["path"])
                                           for item in result["top"]] + [""]
        if "companions" in result:
            extra = result["with_companion"]
            lines += [f"Main path over direct calls plus the deepest overlay or module root ({extra['companion']}): "
                      f"{extra['bytes']} bytes.", "", "| Overlay or module | Deepest root | Bytes | Cycles |",
                      "|---|---|---:|---:|"]
            for name, item in sorted(result["companions"].items(), key=lambda pair: -pair[1]["bytes"]):
                root = item["deepest"][0]["root"] if item["deepest"] else "-"
                lines.append(f"| {name} | {root} | {item['bytes']} | {len(item['cycles'])} |")
            lines.append("")
    return "\n".join(lines)


def combine(report, kind):
    main = report.get("main", {}).get(kind, 0)
    tick = report.get("systick", {}).get(kind, 0)
    fault = report.get("fault", {}).get(kind, 0)
    return main + EXCEPTION_FRAME + tick + (EXCEPTION_FRAME + fault if fault else 0)


def analyse_program(program_dir, elf_name, lto_objects, entries, budget):
    graph, files = program_graph(program_dir, elf_name, lto_objects)
    elf = program_dir / f"{elf_name}.elf"
    taken, _ = address_taken(elf.read_bytes()) if elf.is_file() else ([], [])
    report, worst_target = bound_report(graph, entries, taken)
    combined = {kind: combine(report, kind) for kind in ("direct_bytes", "indirect_bytes")}
    bss_end = elf_symbol(elf.read_bytes(), "core1_bss_end") if elf.is_file() and elf_name != "echo_device" else None
    return {
        "budget": budget,
        "available": CORE1_STACK_TOP - bss_end if bss_end else budget,
        "files": len(files),
        "entries": report,
        "combined": combined["indirect_bytes"],
        "combined_direct": combined["direct_bytes"],
        "combined_note": "main + exception frame + SysTick handler, plus another frame and the fault handler when "
                         "the program has one",
        "worst_indirect_target": worst_target,
        "cycles": graph.cycles(),
        "unknown": sorted(graph.unknown),
        "dynamic": sorted(graph.dynamic),
        "indirect_sites": sum(len(sites) for sites in graph.sites.values()),
        "indirect_callers": len(graph.sites),
        "address_taken": len(taken),
        "top": top_paths(graph),
    }


def companion_names(build):
    device = build / "platform/device"
    return sorted(path.stem for path in device.glob("echo_*.elf")
                  if path.stem.startswith(("echo_module_", "echo_rescan_overlay", "echo_usb_overlay")))


def companion_report(build, name, lto_objects, host):
    graph, files = program_graph(build / "platform/device", name, lto_objects)
    own = set(graph.frames)
    combined = CallGraph()
    combined.frames = {**host.frames, **graph.frames}
    combined.calls = {**host.calls, **graph.calls}
    combined.sites = {**host.sites, **graph.sites}
    combined.finish()
    depths = combined.depths()
    called = {target for function in own for target in graph.calls.get(function, ())}
    roots = sorted((function for function in own if function not in called), key=lambda item: -depths[item][0])
    worst = roots[0] if roots else None
    return {"files": len(files), "functions": len(own), "cycles": [cycle for cycle in graph.cycles()],
            "unknown": sorted(graph.unknown - set(host.frames)),
            "deepest": [{"root": root, "bytes": depths[root][0], "path": list(depths[root][1])} for root in roots[:5]],
            "bytes": depths[worst][0] if worst else 0}


def stack_programs(build):
    device = build / "platform/device"
    programs = {"echo_device": (CORE0_ENTRIES, CORE0_STACK)}
    for name, reserve in CORE1_RESERVES.items():
        programs[name] = (CORE1_ENTRIES, reserve)
    return {name: value for name, value in programs.items() if (device / f"{name}.elf").is_file()}


def lto_objects_of(database):
    objects = set()
    for entry in database:
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        if "-flto" in command and "-fno-lto" not in command:
            output = re.search(r"-o (\S+)", command)
            if output:
                objects.add(str(Path(entry["directory"], output.group(1)).resolve()))
    return objects


def stack_bound(build):
    database = json.loads((build / "compile_commands.json").read_text())
    lto = lto_objects_of(database)
    results = {}
    for name, (entries, budget) in stack_programs(build).items():
        results[name] = analyse_program(build / "platform/device", name, lto, entries, budget)
    if "echo_device" in results:
        host, _ = program_graph(build / "platform/device", "echo_device", lto)
        companions = {name: companion_report(build, name, lto, host) for name in companion_names(build)}
        device = results["echo_device"]
        worst = max(companions, key=lambda name: companions[name]["bytes"], default=None)
        device["companions"] = companions
        device["with_companion"] = {
            "companion": worst,
            "bytes": device["entries"]["main"].get("direct_bytes", 0) + (companions[worst]["bytes"] if worst else 0),
        }
    return results


def findings_markdown(title, new, total, stale, failures):
    lines = [f"# {title}", "", f"Findings: {total}; new against the baseline: {len(new)}; stale baseline entries: "
             f"{len(stale)}; units that did not analyse: {len(failures)}.", ""]
    by_check = {}
    for finding in new:
        by_check.setdefault(finding["check"], []).append(finding)
    for check in sorted(by_check, key=lambda name: -len(by_check[name])):
        lines += [f"## {check} ({len(by_check[check])})", ""]
        lines += [f"- `{item['path']}:{item['line']}` {item['message']} `{item['fingerprint']}`"
                  for item in by_check[check]] + [""]
    if failures:
        lines += ["## Units that did not analyse", ""] + [f"- {name}" for name in failures] + [""]
    return "\n".join(lines)


if __name__ == "__main__":
    sys.exit("run through tools/firmware.py analyze")
