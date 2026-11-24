#!/usr/bin/env python3
"""Check the player's debug report and prepare an on-player acceptance run."""

import argparse
import json
import re
import sys
from pathlib import Path

import echo_debug


SOAK_SCRIPT = """\
# Use English and pause a wired track with 70s left, at low volume.
# Leaves Gain Low and DAC filter Fast, low latency.
open home
set Gain=Low
set DAC filter=Fast, low latency
open nowplaying
wait 1000
play
wait 2000
set Gain=High
set DAC filter=Slow, low latency
soak 1
set Gain=Low
set DAC filter=Fast, low latency
wait 2000
play
wait 1000
open home
reconnect
"""

CHECKLIST = """\
On-player pre-release checklist

- Power cycle the player and confirm it starts reliably.
- Play a track on the 3.5mm and 4.4mm outputs and listen for dropouts.
- Confirm settings and playback state are kept across restarts.
- Verify charging with a USB power source.
- Copy files to and from the player over normal USB file transfer. Eject and verify playback of a copied track.
- In USB DAC mode, open the menu, switch gain, and change the host sample rate during playback. Listen after each change.
"""


def check_report(tool, build, max_underruns):
    checks = []

    def check(name, value, expected, ok):
        checks.append({"name": name, "value": value, "expected": expected, "ok": ok})

    try:
        reports, unreadable = tool.read_reports()
        if not reports and not unreadable:
            unreadable = [{"error": "no_report", "message": "the debug folder holds no report"}]
    except (echo_debug.DebugError, OSError, ValueError) as error:
        code = getattr(error, "code", "io_error" if isinstance(error, OSError) else "bad_report")
        reports, unreadable = [], [{"error": code, "message": str(error)}]

    check("report.readable", unreadable, "at least one report and no unreadable slots", not unreadable)
    path = None
    if reports:
        newest = reports[0]
        path = newest["path"]
        report = newest["report"]
        values = report["values"]
        check("report.truncated", report["truncated"], False, report["truncated"] is False)
        cold_start = values.get("report.cold_start")
        check("report.cold_start", cold_start, 0, cold_start == 0)

        tag = values.get("firmware.build")
        check("firmware.build_clean", tag, "no trailing +", tag is None or not tag.endswith("+"))
        if build is not None:
            clean_tag = tag.removesuffix("+").lower() if tag is not None else None
            check("firmware.build", tag, build, clean_tag == build)

        for name in ("fault.cfsr", "fault.hfsr", "fault.pc"):
            value = values.get(name)
            check(name, value, 0, value == 0)

        underruns = values.get("audio.output_underruns")
        check("audio.output_underruns", underruns, f"0..{max_underruns}",
              isinstance(underruns, int) and 0 <= underruns <= max_underruns)

        for name, allowed in (("usb.writes_after_eject", 0), ("start.last_step_finished", 1)):
            value = values.get(name)
            check(name, value, [None, allowed], value in (None, allowed))

        millivolts = values.get("battery.millivolts")
        check("battery.millivolts", millivolts, "unknown or 3000..4500 mV",
              millivolts is None or isinstance(millivolts, int) and 3000 <= millivolts <= 4500)
        check("events.dropped", values.get("events.dropped"), "absent or 0",
              values.get("events.dropped", 0) == 0)

    recent = [{key: value for key, value in item.items() if key != "report"} for item in reports]
    return {"ok": all(item["ok"] for item in checks), "path": path, "checks": checks, "reports": recent}


def print_scorecard(result):
    outcome = "PASS" if result["ok"] else "FAIL"
    report = result["path"] or "no readable report"
    print(f"{outcome} {report}")
    for check in result["checks"]:
        outcome = "PASS" if check["ok"] else "FAIL"
        value = json.dumps(check["value"])
        expected = f" (expected {check['expected']})" if not check["ok"] else ""
        print(f"  {outcome} {check['name']} = {value}{expected}")
        if check["name"] == "report.cold_start" and check["value"] == 1:
            print(f"  {echo_debug.COLD_START_ADVICE}")
    older = result["reports"][1:]
    if older:
        entries = ", ".join(f"{item['file']} (sequence {item['sequence']})" for item in older)
        print(f"  Earlier reports: {entries}")


def sha7(value):
    if not re.fullmatch(r"[0-9a-fA-F]{7}", value):
        raise argparse.ArgumentTypeError("expected seven hexadecimal digits")
    return value.lower()


def nonnegative_int(value):
    count = int(value)
    if count < 0:
        raise argparse.ArgumentTypeError("expected a nonnegative integer")
    return count


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="score the newest debug report by sequence")
    check.add_argument("--volume", type=Path, help="mounted or extracted internal storage folder")
    check.add_argument("--build", metavar="SHA7", type=sha7, help="expected firmware commit")
    check.add_argument("--max-underruns", type=nonnegative_int, default=0,
                       help="highest passing output underrun count (default %(default)s)")
    check.add_argument("--json", action="store_true", help="emit the scorecard as JSON")
    script = commands.add_parser("soak-script", help="generate a script for a paused track on an English UI")
    script.add_argument("--output", type=Path, help="save to this computer instead of printing the script")
    script.add_argument("--build", metavar="SHA7", type=sha7,
                        help="bind the script to this firmware commit (defaults to any build)")
    commands.add_parser("checklist", help="print the manual on-player release checks")
    return parser


def main(argv=None, host=None):
    args = build_parser().parse_args(argv)
    if args.command == "check":
        result = check_report(echo_debug.Tool(host=host, volume=args.volume), args.build, args.max_underruns)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print_scorecard(result)
        return 0 if result["ok"] else 1
    if args.command == "soak-script":
        try:
            data = echo_debug.prepare_script(SOAK_SCRIPT, args.build or "any")
            if args.output is None:
                print(data.decode("utf-8"), end="")
            else:
                args.output.write_bytes(data)
        except (echo_debug.DebugError, OSError) as error:
            print(f"FAIL soak-script: {error}", file=sys.stderr)
            return 1
    else:
        print(CHECKLIST, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
