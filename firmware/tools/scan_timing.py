import argparse
import json
import statistics
import sys
from pathlib import Path

SCRIPTS = {
    "coverflow": [
        "open coverflow", "wait 3000",
        "down", "wait 1500", "down", "wait 1500", "down", "wait 1500", "up", "wait 1500",
    ],
    "nowplaying": [
        "open home", "wait 1000", "play", "down,down,down,down", "play", "wait 1000",
        "down,down", "play", "wait 1000", "play", "wait 4000",
    ],
    "rescan": ["rescan check", "wait 8000"],
}
SCRIPT_LINE_BUDGET = 12
SCRIPT_BYTES = 640
STEP_LATENCY_LIMIT_MS = 5000


def script_text(name, build):
    lines = ["build " + build] + SCRIPTS[name]
    text = "\n".join(lines) + "\n"
    if len(text.encode("ascii")) > SCRIPT_BYTES:
        raise ValueError("the %s script is longer than %d bytes" % (name, SCRIPT_BYTES))
    return text


def parse_report(text):
    values, events, lines = {}, [], {}
    for raw in text.splitlines():
        key, separator, value = raw.partition(": ")
        if not separator:
            continue
        if key == "event":
            fields = value.split(" ")
            if fields and fields[0].isdigit() and len(fields) >= 2:
                events.append({"ms": int(fields[0]), "kind": fields[1], "detail": fields[2:]})
        elif key == "script.line":
            number, _, rest = value.partition(" ")
            if number.isdigit():
                lines[int(number)] = rest.partition(" ")[2]
        else:
            values[key] = value
    return {"values": values, "events": events, "script_lines": lines}


def read_reports(paths):
    found = []
    for path in paths:
        path = Path(path)
        files = sorted(path.glob("report*.txt")) if path.is_dir() else [path]
        for file in files:
            report = parse_report(file.read_text(errors="replace"))
            report["file"] = str(file)
            sequence = report["values"].get("report.sequence", "0")
            report["sequence"] = int(sequence) if sequence.isdigit() else 0
            found.append(report)
    unique = {report["sequence"]: report for report in found}
    return [unique[sequence] for sequence in sorted(unique)]


def scan_events(report, phase):
    return [event for event in report["events"]
            if event["kind"] == "scan" and event["detail"][:1] == [phase]]


def number_of(event):
    return int(event["detail"][1]) if len(event["detail"]) > 1 and event["detail"][1].isdigit() else 0


def first_scan(report):
    starts = scan_events(report, "start")
    if not starts:
        return None
    start = starts[-1]["ms"]
    library = [event for event in scan_events(report, "library") if event["ms"] >= start]
    passes, pass_start = [], None
    for event in report["events"]:
        if event["kind"] != "scan" or event["ms"] < start:
            continue
        phase = event["detail"][0]
        if phase == "covers":
            pass_start = event["ms"]
        elif phase in ("covers_done", "covers_stopped") and pass_start is not None:
            passes.append({"ms": event["ms"] - pass_start, "end": phase, "number": number_of(event)})
            pass_start = None
    done = [event for event in scan_events(report, "covers_done") if event["ms"] >= start]
    return {
        "browsable_ms": library[0]["ms"] - start if library else None,
        "tracks": number_of(library[0]) if library else None,
        "cover_passes": passes,
        "cover_work_ms": sum(item["ms"] for item in passes),
        "covers": number_of(done[-1]) if done else None,
        "covers_done_ms": done[-1]["ms"] - start if done else None,
    }


def script_line_time(report, wanted):
    lines = report["script_lines"]
    numbers = [number for number, text in lines.items() if text.split(" ")[:len(wanted)] == wanted]
    for event in report["events"]:
        if event["kind"] == "script" and event["detail"][:1] and event["detail"][0].isdigit():
            if int(event["detail"][0]) in numbers:
                return event["ms"]
    return None


def up_to_date(report):
    asked = script_line_time(report, ["rescan", "check"])
    answers = [event["ms"] for event in scan_events(report, "up_to_date")]
    later = [moment for moment in answers if asked is not None and moment >= asked]
    return later[0] - asked if later else None


def covers_between(report, start, end, size=None):
    return [event for event in report["events"]
            if event["kind"] == "cover" and start <= event["ms"] < end
            and (size is None or event["detail"][:1] == [size])]


def found(loads):
    return len([event for event in loads if "found" in event["detail"]])


def next_boundary(report, moment):
    later = [event["ms"] for event in report["events"] if event["ms"] > moment
             and event["kind"] in ("key", "player", "screen")]
    return min(later + [moment + STEP_LATENCY_LIMIT_MS])


def cover_flow(report):
    opened = script_line_time(report, ["open", "coverflow"])
    if opened is None:
        return None
    keys = [event["ms"] for event in report["events"] if event["kind"] == "key" and event["ms"] > opened]
    first_key = keys[0] if keys else opened + STEP_LATENCY_LIMIT_MS
    open_loads = covers_between(report, opened, first_key)
    steps = []
    for moment in keys[:4]:
        following = next_boundary(report, moment)
        loads = covers_between(report, moment, following)
        if loads:
            steps.append(loads[-1]["ms"] - moment)
    return {"open_to_last_cover_ms": open_loads[-1]["ms"] - opened if open_loads else None,
            "open_covers": len(open_loads), "open_found": found(open_loads),
            "step_to_last_cover_ms": steps}


def now_playing(report):
    starts = [event["ms"] for event in report["events"]
              if event["kind"] == "player" and event["detail"][:1] == ["playing"]]
    results = []
    for moment in starts:
        loads = covers_between(report, moment, moment + STEP_LATENCY_LIMIT_MS, "full")
        if loads:
            results.append(loads[0]["ms"] - moment)
    return results


def boot(report):
    values = report["values"]
    events = report["events"]
    screens = [event["ms"] for event in events if event["kind"] == "screen"]
    return {"cold_start": values.get("report.cold_start"), "uptime_ms": values.get("uptime_ms"),
            "first_event_ms": events[0]["ms"] if events else None,
            "first_screen_ms": screens[0] if screens else None}


def lost_starts(report):
    dropped = report["values"].get("events.dropped", "0")
    if not dropped.isdigit() or int(dropped) == 0:
        return []
    kinds = [(event["kind"], event["detail"][:1]) for event in report["events"]]
    lost = []
    timed = [phase for kind, phase in kinds if kind == "scan" and phase != ["up_to_date"]]
    if timed and ("scan", ["start"]) not in kinds:
        lost.append("scan")
    if report["script_lines"] and ("script", ["running"]) not in kinds:
        lost.append("script")
    return lost


def summarize(reports):
    summary = {"reports": [report["file"] for report in reports], "first_scan": None,
               "up_to_date_ms": None, "cover_flow": None, "now_playing_ms": [], "boots": [],
               "lost": []}
    for report in reports:
        summary["lost"] += [(report["file"], what) for what in lost_starts(report)]
        scan = first_scan(report)
        summary["first_scan"] = scan or summary["first_scan"]
        checked = up_to_date(report)
        summary["up_to_date_ms"] = checked if checked is not None else summary["up_to_date_ms"]
        flow = cover_flow(report)
        summary["cover_flow"] = flow or summary["cover_flow"]
        summary["now_playing_ms"] += now_playing(report)
        if report["values"].get("report.cold_start") == "1":
            summary["boots"].append(boot(report))
    return summary


def seconds(value):
    return "-" if value is None else "%.1f s" % (value / 1000)


def milliseconds(values):
    if not values:
        return "-"
    if len(values) == 1:
        return "%d ms" % values[0]
    return "median %d ms, worst %d ms (%d)" % (statistics.median(values), max(values), len(values))


def table(summary):
    scan = summary["first_scan"] or {}
    flow = summary["cover_flow"] or {}
    rows = [
        ("library browsable after the scan starts", seconds(scan.get("browsable_ms"))),
        ("tracks", str(scan.get("tracks", "-"))),
        ("covers done after the scan starts", seconds(scan.get("covers_done_ms"))),
        ("covers stored", str(scan.get("covers", "-"))),
        ("background cover work", "%s in %d pass(es)" % (seconds(scan.get("cover_work_ms")),
                                                        len(scan.get("cover_passes", [])))),
        ("rescan with nothing changed", seconds(summary["up_to_date_ms"])),
        ("Cover Flow open to last cover loaded", milliseconds(
            [flow["open_to_last_cover_ms"]] if flow.get("open_to_last_cover_ms") is not None else [])),
        ("Cover Flow covers at opening, with art", "%s, %s" % (flow.get("open_covers", "-"),
                                                             flow.get("open_found", "-"))),
        ("Cover Flow step to last cover loaded", milliseconds(flow.get("step_to_last_cover_ms", []))),
        ("track start to Now Playing cover loaded", milliseconds(summary["now_playing_ms"])),
    ]
    for boot_row in summary["boots"]:
        rows.append(("cold start: first screen event", "%s ms" % boot_row["first_screen_ms"]))
    for file, what in summary["lost"]:
        rows.append(("start of a measurement lost from the event ring", "%s in %s" % (what, file)))
    width = max(len(name) for name, _ in rows)
    return "\n".join("%-*s  %s" % (width, name, value) for name, value in rows)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Turn the player's debug reports into scan and "
                                                 "cover timings, or print a timing script")
    commands = parser.add_subparsers(dest="command", required=True)
    script = commands.add_parser("script", help="print a run.txt for the debug loop")
    script.add_argument("name", choices=sorted(SCRIPTS))
    script.add_argument("--build", default="any")
    report = commands.add_parser("report", help="summarize report files or folders")
    report.add_argument("paths", nargs="+")
    report.add_argument("--json", action="store_true")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    if arguments.command == "script":
        sys.stdout.write(script_text(arguments.name, arguments.build))
        return 0
    summary = summarize(read_reports(arguments.paths))
    print(json.dumps(summary, indent=1) if arguments.json else table(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
