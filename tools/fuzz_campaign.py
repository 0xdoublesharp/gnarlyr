"""Long libFuzzer campaigns: persistent corpora, dictionaries, merging, artifacts and a summary report."""
from concurrent.futures import ThreadPoolExecutor
import contextlib
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import threading
import time

ARTIFACT_KINDS = ("crash", "leak", "timeout", "oom", "slow-unit")
TIMEOUT_SECONDS = 10
SYNTHETIC_VERSION = "1"
DICTIONARIES = {
    "track_tags": ("tags", "flac", "ogg", "mp4", "riff", "ape", "asf", "dsd", "image"),
    "timed_metadata": ("lyrics", "mp4", "tags"),
    "playlist_reader": ("playlist",),
    "session_journal": ("ds",),
    "library_database": ("ds",),
    "mp4_demux": ("mp4",),
    "store": ("ds",),
    "mix_catalog": ("ds",),
    "settings_codec": ("ds",),
    "cue_sheet": ("cue",),
    "cover_image": ("image",),
    "theme_pack": ("theme",),
    "mp3_probe": ("mp3", "tags"),
    "ogg_track": ("ogg", "tags"),
    "wav_source": ("riff",),
    "vorbis_decoder": ("ogg",),
    "asf_track": ("asf",),
    "ape_track": ("ape",),
    "dsd_track": ("dsd",),
    "fat_volume": ("fat",),
}
MEDIA_TARGETS = {
    ".flac": ("track_tags",),
    ".mp3": ("track_tags", "mp3_probe"),
    ".ogg": ("track_tags", "ogg_track", "vorbis_decoder"),
    ".opus": ("track_tags", "ogg_track", "opus_decode"),
    ".m4a": ("track_tags", "mp4_demux"),
    ".m4b": ("track_tags", "mp4_demux", "timed_metadata"),
    ".wav": ("track_tags", "wav_source"),
    ".aiff": ("track_tags",),
    ".aif": ("track_tags",),
    ".wma": ("track_tags", "asf_track"),
    ".ape": ("track_tags", "ape_track"),
    ".dsf": ("track_tags", "dsd_track"),
    ".dff": ("track_tags", "dsd_track"),
    ".lrc": ("timed_metadata",),
    ".cue": ("cue_sheet",),
    ".m3u": ("playlist_reader",),
    ".m3u8": ("playlist_reader",),
    ".jpg": ("cover_image",),
    ".png": ("cover_image",),
    ".thm": ("theme_pack",),
}
STAT_LINE = re.compile(
    r"^#(\d+):\s+cov: (\d+) ft: (\d+) corp: (\d+) exec/s: (\d+) oom/timeout/crash: (\d+)/(\d+)/(\d+) time: (\d+)s")
INITED_LINE = re.compile(r"INITED cov: (\d+) ft: (\d+) corp: (\d+)/")
EMPTY_CORPUS = re.compile(r"INITED exec/s: \d+ rss")
SANITIZER_LINE = re.compile(r"ERROR: (AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|libFuzzer): ?(\S+)?")


def parse_coverage(output):
    match = None
    for match in INITED_LINE.finditer(output):
        pass
    if match:
        return {"cov": int(match.group(1)), "ft": int(match.group(2)), "files": int(match.group(3))}
    if EMPTY_CORPUS.search(output):
        return {"cov": 0, "ft": 0, "files": 0}
    return None


def parse_fork_log(output):
    stats = []
    for line in output.splitlines():
        match = STAT_LINE.match(line)
        if match:
            values = [int(value) for value in match.groups()]
            stats.append(dict(zip(("execs", "cov", "ft", "corpus", "exec_per_second", "ooms", "timeouts",
                                   "crashes", "seconds"), values)))
    last = stats[-1] if stats else {}
    seconds = last.get("seconds", 0)
    return {
        "execs": last.get("execs", 0),
        "seconds": seconds,
        "exec_per_second": round(last["execs"] / seconds) if seconds else last.get("exec_per_second", 0),
        "cov": last.get("cov", 0),
        "ft": last.get("ft", 0),
        "events": {"ooms": last.get("ooms", 0), "timeouts": last.get("timeouts", 0),
                   "crashes": last.get("crashes", 0)},
        "sanitizer": sorted({" ".join(filter(None, match.groups())) for match in SANITIZER_LINE.finditer(output)}),
    }


def dictionary_text(name, source, extra=()):
    lines = []
    for part in DICTIONARIES.get(name, ()):
        for line in (source / f"{part}.dict").read_text().splitlines():
            if line and line not in lines:
                lines.append(line)
    for value in extra:
        line = escape_dictionary_entry(value)
        if line not in lines:
            lines.append(line)
    return "\n".join(lines) + "\n" if lines else ""


def escape_dictionary_entry(value):
    if isinstance(value, str):
        value = value.encode()
    text = "".join(chr(byte) if 0x20 <= byte < 0x7f and byte not in (0x22, 0x5c) else "\\x%02X" % byte
                   for byte in value)
    return f'"{text}"'


def dictionary_entry_valid(line):
    return re.fullmatch(r'(?:[A-Za-z0-9_]+=)?"(?:[\x20-\x21\x23-\x5b\x5d-\x7e]|\\x[0-9A-Fa-f]{2}|\\\\|\\")*"',
                        line) is not None


def content_name(prefix, data):
    return f"{prefix}-{hashlib.sha1(data).hexdigest()[:16]}"


def ogg_packets(data):
    packets = []
    partial = b""
    offset = 0
    while offset + 27 <= len(data) and data[offset:offset + 4] == b"OggS":
        count = data[offset + 26]
        table = data[offset + 27:offset + 27 + count]
        body = offset + 27 + count
        for lacing in table:
            partial += data[body:body + lacing]
            body += lacing
            if lacing < 255:
                packets.append(partial)
                partial = b""
        offset = body
    return packets


def opus_seeds(data, controls):
    packets = [packet for packet in ogg_packets(data) if not packet.startswith((b"OpusHead", b"OpusTags"))]
    return [control + packet for packet in packets[:4] for control in controls]


def frame_for(name, data, opus_controls):
    if name == "playlist_reader":
        return [b"\xff\x00" + data, b"\x00\x00" + data]
    if name == "ogg_track":
        return [data, data + b"\x00", data + b"\x07"]
    if name == "opus_decode":
        return opus_seeds(data, opus_controls)
    if name == "theme_pack":
        return [data] if data[:1] and data[0] & 3 == 0 else []
    return [data]


def media_seeds(paths, max_lengths, opus_controls):
    seeds = {name: {} for name in max_lengths}
    for path in paths:
        for name in MEDIA_TARGETS.get(path.suffix.lower(), ()):
            if name not in max_lengths:
                continue
            data = path.read_bytes()
            for framed in frame_for(name, data, opus_controls):
                framed = framed[:max_lengths[name]]
                if framed:
                    seeds[name][content_name("media", framed)] = framed
    return seeds


def allowed_media(roots):
    paths = []
    for root in roots:
        if root.is_dir():
            paths.extend(path for path in sorted(root.rglob("*"))
                         if path.is_file() and path.suffix.lower() in MEDIA_TARGETS)
    return paths


@contextlib.contextmanager
def corpus_lock(root, name):
    locks = root / ".locks"
    locks.mkdir(parents=True, exist_ok=True)
    with open(locks / f"{name}.lock", "w") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError(f"another campaign is using the {name} corpus") from None
        yield


def seed_corpus(corpus, seeds):
    corpus.mkdir(parents=True, exist_ok=True)
    added = 0
    for data in seeds:
        path = corpus / content_name("seed", data)
        if not path.exists():
            path.write_bytes(data)
            added += 1
    return added


def fork_command(binary, corpus, artifacts, settings):
    command = [
        str(binary), str(corpus),
        f"-max_total_time={settings['seconds']}",
        "-fork=1", "-ignore_crashes=1", "-ignore_timeouts=1", "-ignore_ooms=1",
        f"-max_len={settings['max_length']}",
        f"-timeout={TIMEOUT_SECONDS}",
        "-rss_limit_mb=2048",
        "-print_final_stats=1",
        f"-artifact_prefix={artifacts}/",
    ]
    if settings.get("seed"):
        command.append(f"-seed={settings['seed']}")
    if settings.get("dictionary"):
        command.append(f"-dict={settings['dictionary']}")
    return command


def coverage_command(binary, corpus, max_length):
    return [str(binary), str(corpus), "-runs=0", f"-max_len={max_length}", f"-timeout={TIMEOUT_SECONDS}",
            "-rss_limit_mb=2048"]


def merge_command(binary, merged, corpus, max_length):
    return [str(binary), "-merge=1", f"-max_len={max_length}", f"-timeout={TIMEOUT_SECONDS}",
            "-rss_limit_mb=2048", str(merged), str(corpus)]


def sanitizer_env(env, suppressions=None):
    options = [option for option in env.get("ASAN_OPTIONS", "").split(":") if option and
               not option.startswith("detect_leaks=")]
    result = {**env, "ASAN_OPTIONS": ":".join([*options, "detect_leaks=1"])}
    if suppressions:
        result["LSAN_OPTIONS"] = f"suppressions={suppressions}:print_suppressions=0"
    return result


def repro_line(binary, artifact, suppressions=None):
    prefix = "ASAN_OPTIONS=detect_leaks=1 "
    if suppressions:
        prefix += f"LSAN_OPTIONS=suppressions={shlex.quote(str(suppressions))} "
    return f"{prefix}{shlex.quote(str(binary))} -runs=1 {shlex.quote(str(artifact))}"


def artifacts_in(directory):
    return sorted(path for path in directory.iterdir()
                  if path.is_file() and path.name.split("-")[0] in {kind.split("-")[0] for kind in ARTIFACT_KINDS}
                  and not path.name.endswith(".repro"))


def write_repros(directory, binary, suppressions=None):
    found = []
    for artifact in artifacts_in(directory):
        repro = artifact.with_name(artifact.name + ".repro")
        if not repro.exists():
            repro.write_text(repro_line(binary, artifact, suppressions) + "\n")
        kind = next(kind for kind in ARTIFACT_KINDS if artifact.name.startswith(kind))
        found.append({"kind": kind, "path": str(artifact), "repro": repro.read_text().strip()})
    return found


def replace_with_merge(corpus, merged):
    retired = corpus.with_name(corpus.name + ".retired")
    if retired.exists():
        shutil.rmtree(retired)
    corpus.rename(retired)
    merged.rename(corpus)
    shutil.rmtree(retired)


def count_files(directory):
    return sum(1 for path in directory.iterdir() if path.is_file())


class Campaign:
    def __init__(self, layout, runner=None, clock=time.monotonic):
        self.layout = layout
        self.runner = runner or self.execute
        self.clock = clock
        self.results = {}
        self.lock = threading.Lock()

    def execute(self, command, log, env):
        with open(log, "a") as handle:
            handle.write("+ " + " ".join(shlex.quote(str(part)) for part in command) + "\n")
            handle.flush()
            completed = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, env=env,
                                       cwd=log.parent)
        return completed.returncode

    def run_logged(self, command, log, env):
        start = log.stat().st_size if log.exists() else 0
        code = self.runner(command, log, env)
        with open(log, errors="replace") as handle:
            handle.seek(start)
            return code, handle.read()

    def run_one(self, name, settings, env):
        layout = self.layout
        corpus = layout["corpus"] / name
        artifacts = layout["report"] / name
        artifacts.mkdir(parents=True, exist_ok=True)
        log = artifacts / "fuzz.log"
        binary = layout["binaries"] / f"fuzz_{name}"
        earlier = {path.name for path in artifacts_in(artifacts)}
        with corpus_lock(layout["corpus"], name):
            added = seed_corpus(corpus, settings["seeds"])
            files_before = count_files(corpus)
            _, output = self.run_logged(coverage_command(binary, corpus, settings["max_length"]), log, env)
            before = parse_coverage(output)
            started = self.clock()
            code, output = self.run_logged(fork_command(binary, corpus, artifacts, settings), log, env)
            fuzzed = parse_fork_log(output)
            merge_code = self.merge(binary, corpus, settings, log, env) if before is not None else None
            _, output = self.run_logged(coverage_command(binary, corpus, settings["max_length"]), log, env)
            after = parse_coverage(output)
            result = {
                "fuzzer": name,
                "exit_code": code,
                "merge_exit_code": merge_code,
                "wall_seconds": round(self.clock() - started),
                "seeds_added": added,
                "files_before": files_before,
                "files_after": count_files(corpus),
                "coverage_before": before,
                "coverage_after": after,
                "dictionary": bool(settings.get("dictionary")),
                **fuzzed,
                "artifacts": [artifact for artifact in write_repros(artifacts, binary, layout.get("suppressions"))
                              if Path(artifact["path"]).name not in earlier],
                "log": str(log),
            }
        self.record(name, result)
        return result

    def merge(self, binary, corpus, settings, log, env):
        merged = corpus.with_name(corpus.name + ".merging")
        if merged.exists():
            shutil.rmtree(merged)
        merged.mkdir()
        code, _ = self.run_logged(merge_command(binary, merged, corpus, settings["max_length"]), log, env)
        if code == 0 and count_files(merged) > 0:
            replace_with_merge(corpus, merged)
        else:
            shutil.rmtree(merged)
        return code

    def record(self, name, result):
        with self.lock:
            self.results[name] = result
            write_summary(self.layout["report"], self.layout["summary"], self.results)

    def run(self, targets, settings_for, env, jobs):
        env = sanitizer_env(env, self.layout.get("suppressions"))

        def guarded(name):
            try:
                return self.run_one(name, settings_for(name), env)
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                result = {"fuzzer": name, "error": str(error), "artifacts": []}
                self.record(name, result)
                return result

        with ThreadPoolExecutor(max_workers=jobs) as pool:
            return list(pool.map(guarded, targets))


def coverage_cell(before, after, key):
    if not before or not after:
        return "not measured: an input crashed during the coverage pass"
    return f"{before[key]} → {after[key]} (+{after[key] - before[key]})"


def summary_markdown(results):
    rows = ["| Fuzzer | Seconds | Executions | exec/s | Coverage (edges) | Features | Corpus files | Crash | Timeout | OOM | Leak |",
            "|---|---:|---:|---:|---|---|---|---:|---:|---:|---:|"]
    artifacts = []
    for name in sorted(results):
        result = results[name]
        if "error" in result:
            rows.append(f"| {name} | error: {result['error']} | | | | | | | | | |")
            continue
        kinds = [artifact["kind"] for artifact in result["artifacts"]]
        rows.append(
            f"| {name} | {result['seconds']} | {result['execs']} | {result['exec_per_second']} | "
            f"{coverage_cell(result['coverage_before'], result['coverage_after'], 'cov')} | "
            f"{coverage_cell(result['coverage_before'], result['coverage_after'], 'ft')} | "
            f"{result['files_before']} → {result['files_after']} | {kinds.count('crash')} | "
            f"{kinds.count('timeout')} | {kinds.count('oom')} | {kinds.count('leak')} |")
        artifacts.extend(f"- `{name}` {artifact['kind']}: `{artifact['repro']}`" for artifact in result["artifacts"])
        if result.get("sanitizer") and not result["artifacts"]:
            artifacts.append(f"- `{name}` reported {', '.join(result['sanitizer'])} without leaving an input; "
                             f"see `{result['log']}`")
    total = sum(result.get("execs", 0) for result in results.values())
    lines = ["# Fuzz campaign summary", "", f"Fuzzers: {len(results)}; executions: {total}.", "", *rows, ""]
    lines += ["## Artifacts", "", *(artifacts or ["None."]), ""]
    return "\n".join(lines)


def write_summary(report, stem, results):
    report.mkdir(parents=True, exist_ok=True)
    (report / f"{stem}.json").write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    (report / f"{stem}.md").write_text(summary_markdown(results))


def campaign_layout(scratch, build_name, now=None):
    now = now or datetime.datetime.now()
    return {
        "corpus": scratch / "data/fuzz-corpus",
        "seeds": scratch / "data/fuzz-seeds" / f"synthetic-v{SYNTHETIC_VERSION}",
        "report": scratch / "reports/fuzz" / now.strftime("%Y-%m-%d"),
        "summary": "summary-" + now.strftime("%H%M%S"),
        "binaries": scratch / "build" / build_name / "fuzz/tests/fuzz",
        "dictionaries": scratch / "tmp" / build_name / "fuzz-dictionaries",
    }


def duration_seconds(minutes, hours):
    if hours is not None:
        return int(hours * 3600)
    return int((minutes if minutes is not None else 10) * 60)
