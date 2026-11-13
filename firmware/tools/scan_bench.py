"""Time a library scan and album-art display in the ARM emulator on a generated music volume.

Builds (or reuses) a deterministic volume of N tracks on Scratch, boots the device image on it,
times the first scan, a requested rescan with nothing changed, the music check after a USB session
and the cover reads behind Home, Cover Flow and Now Playing, then prints a table and writes JSON.
"""
import argparse
import json
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from echo_emulator import bench, music_library

DEFAULT_BUILD = TOOLS.parent / "build" / "device"
DEFAULT_REPORTS = Path("/Volumes/Scratch/SnowskyEcho/reports/scan-bench")
STEPS = ("first", "rescan", "usb", "display", "nocovers")
ROW_ORDER = [
    ("scan", "directory walk", ("walk",), None),
    ("scan", "tag reads", ("tags",), None),
    ("scan", "CUE sheets", ("cue",), None),
    ("scan", "M4B chapters", ("chapters",), None),
    ("scan", "builder: add tracks", ("builder add",), None),
    ("scan", "builder: sort", ("builder sort",), None),
    ("scan", "builder: rank", ("builder rank",), None),
    ("scan", "builder: model", ("builder model",), None),
    ("scan", "builder: encode + write", ("builder encode", "builder finish"), None),
    ("scan", "builder: other", ("builder prepare", "builder paths", "builder album artists",
                                "playlists"), None),
    ("covers", "find art: index, stamps", ("find art",), ("cpu", "keep alive")),
    ("covers", "find art: folder listings", ("find art", "find art: tags"), ("list",)),
    ("covers", "find art: file opens", ("find art", "find art: tags", "decode"), ("open",)),
    ("covers", "find art: tag re-reads", ("find art: tags",), ("cpu", "read", "keep alive")),
    ("covers", "find art: other reads", ("find art",), ("read",)),
    ("covers", "image decode: compute", ("decode",), ("cpu", "keep alive")),
    ("covers", "image decode: file reads", ("decode",), ("read",)),
    ("covers", "covers.bin writes", None, ("write",)),
    ("other", "setup and hand-over", None, None),
]
COLUMNS = ("ms", "instructions", "waits", "read_commands", "read_sectors", "write_commands",
           "write_sectors")


def grouped(phases):
    rows = []
    used = set()
    for stage, label, names, kinds in ROW_ORDER:
        total = dict.fromkeys(COLUMNS, 0)
        for index, phase in enumerate(phases):
            if index in used or phase["stage"] != stage:
                continue
            if names is not None and phase["phase"] not in names:
                continue
            if kinds is not None and phase["kind"] not in kinds:
                continue
            used.add(index)
            for column in COLUMNS:
                total[column] += phase[column]
        rows.append(dict(total, stage=stage, label=label))
    leftovers = [phase for index, phase in enumerate(phases) if index not in used]
    if leftovers:
        total = dict.fromkeys(COLUMNS, 0)
        for phase in leftovers:
            for column in COLUMNS:
                total[column] += phase[column]
        rows.append(dict(total, stage="other", label="unassigned"))
    return rows


def scan_table(title, run):
    lines = ["", "%s: %.1f s emulated (%.2f G instructions, %.1f s eMMC waits at %s MHz), "
             "%.0f s wall, %s" % (title, run["emulated_ms"] / 1000, run["instructions"] / 1e9,
                                  run["emmc_wait_cycles"] / max(run["core_hz"]) if run["core_hz"] else 0,
                                  "/".join(str(hz // 1000000) for hz in run["core_hz"]),
                                  run["wall_seconds"], "; ".join(run["toasts"][-1:]))]
    if run.get("browsable_ms") is not None:
        lines.append("  library browsable at %.1f s (scan pass %.1f s); cover pass %s" % (
            run["browsable_ms"] / 1000, run["scan_pass_ms"] / 1000,
            "%.1f s, in the background" % (run["cover_pass_ms"] / 1000)
            if run.get("cover_pass_ms") is not None else "inside the scan pass"))
    for step in run.get("build_progress") or []:
        lines.append("  shown \"%s\" for %.1f ms: %d updates, last %d%%, longest gap %.1f ms" % (
            step["phase"], step["ms"], step["updates"], step["last_percent"], step["longest_gap_ms"]))
    hand = run.get("hand_over")
    if hand:
        lines.append("  after saving: %.1f ms to the player, %d notices, last \"%s\", longest gap %.1f ms" % (
            hand["ms"], hand["notices"], hand["last"], hand["longest_gap_ms"]))
    lines.append("  %-30s %9s %8s %8s %8s %9s %8s %8s" % (
        "phase", "ms", "M instr", "wait ms", "rd cmds", "rd sect", "wr cmds", "wr sect"))
    hz = max(run["core_hz"]) if run["core_hz"] else 1
    for row in grouped(run["profile"]["phases"]):
        if not any(row[column] for column in COLUMNS):
            continue
        lines.append("  %-30s %9.1f %8.1f %8.1f %8d %9d %8d %8d" % (
            row["stage"][0] + ": " + row["label"], row["ms"], row["instructions"] / 1e6,
            row["waits"] * 1000 / hz, row["read_commands"], row["read_sectors"],
            row["write_commands"], row["write_sectors"]))
    lines.append("  decoded covers %d, %.1f M instructions and %.1f ms per decode; counts %s" % (
        run["decoded_covers"], run["instructions_per_decode"] / 1e6, run["ms_per_decode"],
        run["counts"]))
    lines.append("  then the fingerprint walk for scan.bin: " + walk_text(run["fingerprint_walk_after_scan"]))
    top = ", ".join("%s %.1f%%" % (item["name"], item["share"] * 100)
                    for item in run["profile"]["top_functions"][:10])
    lines.append("  top functions by sampled instructions: " + top)
    return lines


def value(text):
    return "-" if text is None else str(text)


def display_table(title, result):
    lines = ["", title]
    home = result.get("home")
    if home:
        cold, cycle, warm = home["cold"], home["cycle"], home["warm_return"]
        if cold:
            lines.append("  Home cold: load %s ms, load to drawn %s ms, %s card reads, %s albums tried" % (
                cold["load_ms"], cold["load_to_drawn_ms"], cold["read_commands"], cold["albums_tried"]))
        if cycle:
            lines.append("  Home next album: %s albums tried, %s ms loading, %s card reads, first try to drawn %s ms" % (
                cycle["albums_tried"], cycle["load_ms"], cycle["read_commands"], value(cycle["first_try_to_drawn_ms"])))
        lines.append("  Home warm (Back to Home): key to frame %s ms, %s card reads" % (
            value(warm["to_first_frame_ms"]), warm["summary"]["read_commands"]))
    flow = result["cover_flow"]
    entry = flow["entry"]
    lines.append("  Cover Flow open: key to frame %s ms, to last cover drawn %s ms, %s" % (
        value(entry["to_first_frame_ms"]), value(entry["to_last_cover_drawn_ms"]), entry["summary"]))
    for label in ("steps", "steps_back"):
        steps = flow[label]
        frames = [step["to_first_frame_ms"] for step in steps if step["to_first_frame_ms"] is not None]
        drawn = [step["to_last_cover_drawn_ms"] for step in steps if step["to_last_cover_drawn_ms"] is not None]
        commands = [step["summary"]["read_commands"] for step in steps]
        lines.append("  Cover Flow %s: key to frame %s ms, key to last cover drawn %s ms, card reads %s" % (
            "step" if label == "steps" else "step back", frames, drawn, commands))
    taps, hold = flow["taps"], flow["hold"]
    lines.append("  Cover Flow %d taps every %d ms: key to frame %s ms (worst %s), %s, frames %s" % (
        taps["taps"], taps["gap_ms"], taps["key_to_frame_ms"], value(taps["worst_key_to_frame_ms"]),
        taps["summary"], taps["frames"]))
    lines.append("  Cover Flow held %d ms: %s positions, %s, frames %s, release to last cover drawn %s ms" % (
        hold["hold_ms"], hold["positions_moved"], hold["during"], hold["frames"],
        value(hold["release_to_last_cover_drawn_ms"])))
    playing = result.get("now_playing")
    if playing:
        for label, key in (("open", "open"), ("skip to next album", "skip_to_next_album"),
                           ("skip back", "skip_back"), ("Back to the list", "back_to_list"),
                           ("Home while playing", "home_while_playing")):
            step = playing[key]
            extra = ""
            if "track_start_to_cover_drawn_ms" in step:
                extra = ", track start to cover drawn %s ms" % value(step["track_start_to_cover_drawn_ms"])
            lines.append("  Now Playing %s: key to frame %s ms, key to cover drawn %s ms, load start to drawn %s ms%s, %s" % (
                label, value(step["to_first_frame_ms"]), value(step["to_last_cover_drawn_ms"]),
                value(step["last_load_to_drawn_ms"]), extra, step["summary"]))
    return lines


def walk_text(walk):
    if walk is None:
        return "not seen"
    return "%s ms (%.1f M instructions, %d card reads, %d sectors)" % (
        walk["ms"], walk["instructions"] / 1e6, walk["read_commands"], walk["read_sectors"])


def usb_table(result):
    return ["", "Music check after a USB session: fingerprint walk %s, cable to walk end %s ms, "
            "rescan started %s" % (walk_text(result["walk"]), value(result["from_cable_ms"]),
                                   result["rescan_started"])]


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tracks", type=int, default=4000)
    parser.add_argument("--seed", default="1")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--report", type=Path, help="JSON file to write (default: Scratch reports)")
    parser.add_argument("--library-root", type=Path, default=music_library.SCRATCH)
    parser.add_argument("--steps", default=",".join(STEPS),
                        help="comma-separated subset of " + ",".join(STEPS))
    parser.add_argument("--build-library-only", action="store_true")
    parser.add_argument("--scan-pass-only", action="store_true",
                        help="end the first scan when the library is written, before the cover pass")
    return parser.parse_args(argv)


def log(text):
    print(text, flush=True)


def run(arguments):
    steps = [step for step in arguments.steps.split(",") if step]
    unknown = set(steps) - set(STEPS)
    if unknown:
        raise SystemExit("unknown steps: " + ", ".join(sorted(unknown)))
    started = time.time()
    volume = music_library.cached(arguments.tracks, arguments.seed, arguments.library_root, log)
    report = {"tracks": arguments.tracks, "seed": arguments.seed, "library": volume.manifest["summary"],
              "volume": str(volume.directory), "wall_seconds": {}}
    report["wall_seconds"]["library"] = round(time.time() - started, 1)
    if arguments.build_library_only:
        return report
    artifacts = bench.Artifacts(arguments.build_dir)
    report["build"] = artifacts.tag()
    log("first scan of %d tracks on %s" % (arguments.tracks, artifacts.tag()))
    moment = time.time()
    session, first = bench.first_scan(artifacts, volume, log, not arguments.scan_pass_only)
    report["first_scan"] = first.result()
    report["wall_seconds"]["first_scan"] = round(time.time() - moment, 1)
    report["assumptions"] = bench.assumptions(volume, report["first_scan"])
    after_scan = session
    if "rescan" in steps:
        log("rescan with nothing changed")
        moment = time.time()
        _, rescan, row = bench.requested_rescan(after_scan, artifacts, log)
        report["rescan"] = rescan.result()
        report["rescan"]["settings_presses"] = row
        report["wall_seconds"]["rescan"] = round(time.time() - moment, 1)
    if "usb" in steps:
        log("music check after a USB session")
        moment = time.time()
        report["usb_music_check"] = bench.usb_music_check(after_scan, artifacts, log)
        report["wall_seconds"]["usb"] = round(time.time() - moment, 1)
    for step, covers in (("display", True), ("nocovers", False)):
        if step in steps:
            log("album art display%s" % ("" if covers else " without covers.bin"))
            moment = time.time()
            _, result = bench.display(after_scan, artifacts, log, covers)
            report["display" if covers else "display_without_covers"] = result
            report["wall_seconds"][step] = round(time.time() - moment, 1)
    report["wall_seconds"]["total"] = round(time.time() - started, 1)
    return report


def table(report):
    lines = ["Library: %d tracks requested, %s" % (report["tracks"], json.dumps(report["library"]))]
    if "assumptions" in report:
        lines.append("Assumptions: " + json.dumps(report["assumptions"]))
    if "first_scan" in report:
        lines += scan_table("First scan", report["first_scan"])
    if "rescan" in report and report["rescan"].get("shortcut"):
        rescan = report["rescan"]
        lines += ["", "Rescan, nothing changed: %.1f s emulated from the key to \"%s\" (fingerprint walk %s)" % (
            rescan["emulated_ms"] / 1000, "; ".join(rescan["toasts"][:1]), rescan["fingerprint_walk"])]
    elif "rescan" in report:
        lines += scan_table("Rescan, nothing changed", report["rescan"])
    if "usb_music_check" in report:
        lines += usb_table(report["usb_music_check"])
    if report.get("display"):
        lines += display_table("Album art with covers.bin", report["display"])
    if report.get("display_without_covers"):
        lines += display_table("Album art without covers.bin", report["display_without_covers"])
    lines.append("")
    lines.append("Wall seconds: " + json.dumps(report["wall_seconds"]))
    return "\n".join(lines)


def main(argv=None):
    arguments = parse_arguments(argv)
    report = run(arguments)
    path = arguments.report or DEFAULT_REPORTS / ("scan-bench-%s-t%d-%s.json" % (
        report.get("build", "library"), arguments.tracks, time.strftime("%Y%m%d-%H%M%S")))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=1, ensure_ascii=False))
    print(table(report))
    print("report: %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
