#!/usr/bin/env python3
"""Host side of the Doublesharp Echo debug loop.

Reads what a developer build of the firmware leaves in its debug folder on the player's
internal drive, over ordinary USB mass storage. Every command prints one JSON object; a
failure prints {"ok": false, "error": {"code", "message"}} and exits with status 1.

The tool finds the player by its USB identity and its internal drive by name and size. It
never writes, never formats, never opens a raw device and never reads the microSD card's
volume. `--volume PATH` names the internal drive's folder directly: for tests, and for a
volume extracted from an emulator run.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = json.loads((ROOT / "firmware" / "identity.json").read_text(encoding="utf-8"))
DATA_DIRECTORY = IDENTITY["data_directory"]
DEBUG_DIRECTORY = "debug"
REPORT_FILES = tuple("report%d.txt" % slot for slot in range(3))
COLD_START_ADVICE = ("the newest report was written just after a cold start and holds nothing from before it: "
                     "leave the player switched on when it is plugged in or reconnects")
REPORT_SCHEMA = 1
REPORT_BYTES = 16384
SCHEMA_KEY = "debug_report"
TEXT_KEY_PREFIXES = ("firmware.",)
USB_VENDOR_ID = 0x1209
USB_PRODUCT_ID = 0x0001
INTERNAL_MEDIUM = "Internal storage"
CARD_MEDIUM = "SD card"
DEFAULT_VOLUME_NAME = "ECHO"
INTERNAL_BYTES = range(1 << 30, (8 << 30) + 1)
SAMPLE_MS = 1
DEFAULT_TOP = 25
ARM_NM = "arm-none-eabi-nm"
ARM_NM_FALLBACK = Path("/Volumes/Scratch/SnowskyEcho/tools/arm-15.3-expanded/Payload/bin") / ARM_NM
NM_LINE = re.compile(r"^(\S+)\s*\|([0-9a-f]+)\|[^|]*\|\s*FUNC\|([0-9a-f]+)\|[^|]*\|[^\t]*(?:\t(.*):(\d+))?$")
MAP_TEXT = ".text."
SOURCE_ROOTS = ("/firmware/src/", "/firmware/platform/device/", "/firmware/platform/",
                "/firmware/third_party/", "/firmware/")
UNATTRIBUTED = "(no function)"
UNKNOWN_FILE = "(unknown file)"


class DebugError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message

    def as_json(self):
        return {"ok": False, "error": {"code": self.code, "message": self.message}}


class MacHost:
    """What the tool needs from macOS: the USB device tree and ejecting a mounted drive."""

    def usb_tree(self):
        result = subprocess.run(["system_profiler", "SPUSBDataType", "-json"], check=False,
                                capture_output=True, text=True)
        if result.returncode != 0:
            raise DebugError("usb_listing_failed", result.stderr.strip() or "system_profiler failed")
        return json.loads(result.stdout)

    def eject(self, mount_point):
        result = subprocess.run(["diskutil", "eject", str(mount_point)], check=False,
                                capture_output=True, text=True)
        if result.returncode != 0:
            raise DebugError("eject_failed", result.stderr.strip() or result.stdout.strip())


def number(text):
    return int(text, 0) if isinstance(text, str) else int(text or 0)


def usb_entries(node):
    if isinstance(node, list):
        for item in node:
            yield from usb_entries(item)
    elif isinstance(node, dict):
        if "vendor_id" in node or "product_id" in node:
            yield node
        for value in node.values():
            if isinstance(value, (list, dict)):
                yield from usb_entries(value)


def is_player(entry):
    try:
        vendor = number(str(entry.get("vendor_id", "0")).split()[0])
        product = number(str(entry.get("product_id", "0")).split()[0])
    except ValueError:
        return False
    return (vendor, product) == (USB_VENDOR_ID, USB_PRODUCT_ID) and entry.get("_name") == IDENTITY["name"]


def medium_volumes(medium):
    return [{"name": volume.get("_name", ""), "mount_point": volume.get("mount_point"),
             "bytes": number(volume.get("size_in_bytes", 0)), "medium": medium.get("_name", "")}
            for volume in medium.get("volumes", [])]


def find_players(tree):
    return [entry for entry in usb_entries(tree) if is_player(entry)]


def internal_volume(player, volume_name):
    """The mounted internal drive of one player entry, or a DebugError saying what is wrong."""
    media = player.get("Media", [])
    internal = [medium for medium in media if INTERNAL_MEDIUM.lower() in medium.get("_name", "").lower()]
    if len(internal) != 1:
        names = ", ".join(medium.get("_name", "?") for medium in media) or "none"
        raise DebugError("internal_drive_not_identified",
                         f"expected one drive named {INTERNAL_MEDIUM!r} on the player, found: {names}")
    volumes = [volume for volume in medium_volumes(internal[0]) if volume["mount_point"]]
    if len(volumes) != 1:
        raise DebugError("internal_drive_not_mounted",
                         f"the internal drive has {len(volumes)} mounted volumes, expected 1")
    volume = volumes[0]
    if volume["name"] != volume_name:
        raise DebugError("wrong_volume_name",
                         f"the internal drive's volume is named {volume['name']!r}, expected {volume_name!r}")
    if volume["bytes"] not in INTERNAL_BYTES:
        raise DebugError("wrong_volume_size",
                         f"the internal drive's volume is {volume['bytes']} bytes, outside the expected range")
    return volume


def card_volumes(player):
    return [volume for medium in player.get("Media", [])
            if CARD_MEDIUM.lower() in medium.get("_name", "").lower()
            for volume in medium_volumes(medium) if volume["mount_point"]]


def parse_value(text):
    if text == "unknown":
        return None
    if re.fullmatch(r"\d+", text):
        return int(text)
    if re.fullmatch(r"0x[0-9a-fA-F]+", text):
        return int(text, 16)
    return text


def parse_text(text):
    return None if text == "unknown" else text


def parse_event(text):
    fields = text.split(" ", 2)
    if len(fields) < 2 or not fields[0].isdigit():
        raise DebugError("bad_report", f"cannot read the event line {text!r}")
    return {"time_ms": int(fields[0]), "kind": fields[1], "detail": fields[2] if len(fields) > 2 else ""}


def parse_row(text, buckets):
    try:
        fields = [int(field, 16) for field in text.split()]
    except ValueError:
        raise DebugError("bad_report", f"cannot read the profile row {text!r}") from None
    if len(fields) < 2:
        raise DebugError("bad_report", f"the profile row {text!r} has no counts")
    for offset, count in enumerate(fields[1:]):
        buckets[fields[0] + offset] = count


def parse_sample(text):
    fields = text.split()
    if len(fields) != 4 or not all(field.isdigit() for field in fields) or len(fields[3]) != 5:
        raise DebugError("bad_report", f"cannot read the power sample {text!r}")
    vbus, charge_pin, playing, usb_powered, usb_kept_on = (digit == "1" for digit in fields[3])
    return {"minute": int(fields[0]), "millivolts": int(fields[1]), "percent": int(fields[2]),
            "vbus": vbus, "charge_pin": charge_pin, "playing": playing, "usb_powered": usb_powered,
            "usb_kept_on": usb_kept_on}


def report_body(text):
    """The report's lines up to its end line, checked against the length that line states."""
    match = re.search(r"^truncated: ([01])\nend: (\d+)\n", text, re.MULTILINE)
    if match is None:
        raise DebugError("incomplete_report", "the report has no end line: it was cut off or is not a report")
    if int(match.group(2)) != len(text[:match.start()].encode("utf-8")):
        raise DebugError("incomplete_report", "the report's end line does not match its length")
    if text[match.end():].strip("\n"):
        raise DebugError("incomplete_report", "the report has text after its end line")
    return text[:match.start()], match.group(1) == "1"


def parse_report(text):
    """A report file's text as a dictionary; DebugError when it is not a complete report."""
    first = text.split("\n", 1)[0]
    if not first.startswith(SCHEMA_KEY + ": "):
        raise DebugError("not_a_report", "the file does not start with the report's schema line")
    schema = parse_value(first.split(": ", 1)[1])
    if schema != REPORT_SCHEMA:
        raise DebugError("unsupported_schema", f"report schema {schema!r}; this tool reads schema {REPORT_SCHEMA}")
    body, truncated = report_body(text)
    values, events, buckets, unread, samples = {}, [], {}, [], []
    for line in body.splitlines():
        key, separator, value = line.partition(": ")
        if not separator:
            unread.append(line)
        elif key == "event":
            events.append(parse_event(value))
        elif key == "profile.row":
            parse_row(value, buckets)
        elif key == "power.sample":
            samples.extend(parse_sample(sample) for sample in value.split(";"))
        else:
            values[key] = parse_text(value) if key.startswith(TEXT_KEY_PREFIXES) else parse_value(value)
    return {"schema": schema, "truncated": truncated, "values": values, "events": events,
            "power_samples": samples,
            "profile_buckets": {str(index): count for index, count in sorted(buckets.items())},
            "unread_lines": unread}


def source_name(path):
    name = path.replace("\\", "/")
    for root in SOURCE_ROOTS:
        if root in name:
            return name.split(root, 1)[1]
    return Path(name).name


def map_objects(map_text):
    """Function address -> object, for the functions the linker map names an object for."""
    sys.path.insert(0, str(ROOT / "firmware" / "tools"))
    import size_report
    return {piece.address & ~1: size_report.object_name(piece.origin)
            for piece in size_report.parse_map(map_text)
            if piece.section.startswith(MAP_TEXT) and size_report.LTO_OBJECT not in piece.origin}


def parse_nm(text, objects=None):
    """Functions from `nm -S -l --defined-only -f sysv`: sorted (address, size, name, file).

    A function nm has no line for (a library built without debug information) is named by
    its object from the linker map, when the map is given.
    """
    functions = {}
    for line in text.splitlines():
        match = NM_LINE.match(line)
        if match and int(match.group(3), 16) > 0:
            address = int(match.group(2), 16) & ~1
            file = source_name(match.group(4)) if match.group(4) else (objects or {}).get(address, UNKNOWN_FILE)
            functions.setdefault(address, (address, int(match.group(3), 16), match.group(1), file))
    return sorted(functions.values())


def spread_bucket(first, size, count, functions, start, totals):
    """Share one bucket's samples among the functions in it, by the bytes each has there."""
    last = first + size
    overlaps = []
    index = start
    while index < len(functions) and functions[index][0] < last:
        address, length, name, file = functions[index]
        shared = min(last, address + length) - max(first, address)
        if shared > 0:
            overlaps.append((shared, name, file))
        index += 1
    covered = sum(shared for shared, _, _ in overlaps)
    if covered == 0:
        totals[(UNATTRIBUTED, UNKNOWN_FILE)] = totals.get((UNATTRIBUTED, UNKNOWN_FILE), 0) + count
        return
    for shared, name, file in overlaps:
        totals[(name, file)] = totals.get((name, file), 0) + count * shared / covered


def first_function_reaching(functions, address, start):
    while start < len(functions) and functions[start][0] + functions[start][1] <= address:
        start += 1
    return start


def ranked(totals, samples, top):
    rows = sorted(totals.items(), key=lambda item: (-item[1], item[0]))
    return [{"name": name, "ms": round(count * SAMPLE_MS, 2),
             "percent": round(100 * count / samples, 2) if samples else 0.0}
            for name, count in rows[:top]]


def symbolise(report, nm_text, top=DEFAULT_TOP, map_text=None):
    """Time per function and per source file from a parsed report's histogram and nm output.

    A bucket covers several functions, so its samples are shared among them by the bytes each
    has in the bucket: the per-function figures are estimates, the per-file ones less so.
    """
    values = report["values"]
    needed = ("profile.samples", "profile.code_base", "profile.bucket_bytes", "profile.buckets")
    if any(not isinstance(values.get(key), int) for key in needed):
        raise DebugError("no_profile", "the report has no profile figures")
    functions = parse_nm(nm_text, map_objects(map_text) if map_text else None)
    if not functions:
        raise DebugError("no_symbols", "nm listed no functions for the ELF")
    base, size, samples = values["profile.code_base"], values["profile.bucket_bytes"], values["profile.samples"]
    per_function = {}
    start = 0
    for index, count in sorted((int(key), count) for key, count in report["profile_buckets"].items()):
        if index >= values["profile.buckets"]:
            raise DebugError("bad_report", f"profile bucket {index} is outside the histogram")
        start = first_function_reaching(functions, base + index * size, start)
        spread_bucket(base + index * size, size, count, functions, start, per_function)
    per_file = {}
    by_name = {}
    for (name, file), count in per_function.items():
        per_file[file] = per_file.get(file, 0) + count
        by_name[f"{name} ({file})" if name != UNATTRIBUTED else name] = count
    code = sum(report["profile_buckets"].values())
    shares = {"code": code, "idle": values.get("profile.idle"), "overlay": values.get("profile.overlay"),
              "other": values.get("profile.other")}
    counted = sum(value or 0 for value in shares.values())
    return {"state": values.get("profile.state"), "sample_ms": SAMPLE_MS, "samples": samples,
            "consistent": counted == samples, "shares_ms": shares, "bucket_bytes": size,
            "functions": ranked(by_name, samples, top), "files": ranked(per_file, samples, top)}


def run_nm(elf, nm=None):
    tool = nm or shutil.which(ARM_NM) or (str(ARM_NM_FALLBACK) if ARM_NM_FALLBACK.is_file() else None)
    if tool is None:
        raise DebugError("nm_missing", f"{ARM_NM} is not on PATH; pass --nm")
    if not Path(elf).is_file():
        raise DebugError("elf_missing", f"{elf} is not a file")
    result = subprocess.run([tool, "-S", "-l", "--defined-only", "-f", "sysv", str(elf)], check=False,
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise DebugError("nm_failed", result.stderr.strip() or "nm failed")
    return result.stdout


class Tool:
    """The operations behind both the command line and the MCP server."""

    def __init__(self, host=None, volume=None, volume_name=DEFAULT_VOLUME_NAME):
        self.host = host or MacHost()
        self.volume = Path(volume) if volume is not None else None
        self.volume_name = volume_name

    def override(self):
        if not self.volume.is_dir():
            raise DebugError("volume_missing", f"{self.volume} is not a folder")
        if not (self.volume / DATA_DIRECTORY).is_dir():
            raise DebugError("not_a_player_volume",
                             f"{self.volume} has no {DATA_DIRECTORY} folder: it is not the player's internal drive")
        return {"connected": True, "source": "override", "volume": str(self.volume), "usb": None}

    def discover(self):
        players = find_players(self.host.usb_tree())
        if not players:
            return {"connected": False, "source": "usb", "volume": None, "usb": None}
        if len(players) > 1:
            raise DebugError("several_players", f"{len(players)} players are connected; connect one")
        player = players[0]
        volume = internal_volume(player, self.volume_name)
        usb = {"name": player.get("_name"), "serial": player.get("serial_num"),
               "internal": volume, "card_volumes": card_volumes(player)}
        return {"connected": True, "source": "usb", "volume": volume["mount_point"], "usb": usb}

    def locate(self):
        return self.override() if self.volume is not None else self.discover()

    def root(self):
        found = self.locate()
        if not found["connected"]:
            raise DebugError("player_not_connected",
                             f"no {IDENTITY['name']} is on USB as a storage device")
        return Path(found["volume"])

    def debug_directory(self):
        return self.root() / DATA_DIRECTORY / DEBUG_DIRECTORY

    def read_reports(self, directory=None):
        """Every report file on the drive, newest first, and the files that could not be read."""
        directory = directory or self.debug_directory()
        reports, unreadable = [], []
        for name in REPORT_FILES:
            path = directory / name
            if not path.is_file():
                continue
            try:
                if path.stat().st_size > 4 * REPORT_BYTES:
                    raise DebugError("bad_report", f"{path} is {path.stat().st_size} bytes; a report is {REPORT_BYTES}")
                parsed = parse_report(path.read_bytes().decode("utf-8", "replace"))
            except DebugError as error:
                unreadable.append({"file": name, "error": error.code, "message": error.message})
                continue
            values = parsed["values"]
            reports.append({"file": name, "path": str(path), "sequence": values.get("report.sequence"),
                            "reason": values.get("report.reason"),
                            "cold_start": values.get("report.cold_start") == 1,
                            "uptime_ms": values.get("uptime_ms"), "report": parsed})
        reports.sort(key=lambda item: item["sequence"] if isinstance(item["sequence"], int) else -1,
                     reverse=True)
        return reports, unreadable

    def read_report(self):
        """The newest report's path and contents."""
        directory = self.debug_directory()
        reports, unreadable = self.read_reports(directory)
        if not reports and unreadable:
            raise DebugError(unreadable[0]["error"], f"{unreadable[0]['file']}: {unreadable[0]['message']}")
        if not reports:
            raise DebugError("no_report", f"{directory} holds no report: the player has not run a developer "
                                          "build with this drive since it was last cleared")
        return Path(reports[0]["path"]), reports[0]["report"]

    def status(self):
        found = self.locate()
        result = {"ok": True, **found, "report_present": False, "newest_report": None,
                  "cold_start_only": False}
        if found["connected"]:
            reports, _ = self.read_reports(Path(found["volume"]) / DATA_DIRECTORY / DEBUG_DIRECTORY)
            if reports:
                newest = {key: value for key, value in reports[0].items() if key != "report"}
                result.update(report_present=True, newest_report=newest, cold_start_only=newest["cold_start"])
            if result["cold_start_only"]:
                result["advice"] = COLD_START_ADVICE
        return result

    def report(self):
        self.root()
        reports, unreadable = self.read_reports()
        path, parsed = self.read_report()
        return {"ok": True, "path": str(path), "report": parsed, "reports": reports, "unreadable": unreadable}

    def profile(self, elf, nm=None, top=DEFAULT_TOP, nm_text=None):
        path, parsed = self.read_report()
        build = parsed["values"].get("firmware.build")
        text = nm_text if nm_text is not None else run_nm(elf, nm)
        linker_map = Path(elf).with_suffix(".map")
        map_text = linker_map.read_text(encoding="utf-8", errors="replace") if linker_map.is_file() else None
        return {"ok": True, "path": str(path), "elf": str(elf), "firmware_build": build,
                "profile": symbolise(parsed, text, top, map_text)}



class JsonParser(argparse.ArgumentParser):
    def error(self, message):
        print(json.dumps(DebugError("usage", message).as_json()))
        raise SystemExit(2)


def build_parser():
    parser = JsonParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--volume", type=Path,
                        help="the internal drive's folder, instead of finding the player on USB")
    parser.add_argument("--volume-name", default=DEFAULT_VOLUME_NAME,
                        help="the internal drive's volume name (default %(default)s)")
    commands = parser.add_subparsers(dest="command", required=True, parser_class=JsonParser)
    commands.add_parser("status", help="is the player on USB, and which volume is its internal drive")
    commands.add_parser("report", help="read and parse the player's debug report")
    profile = commands.add_parser("profile", help="time per function and file from the report's histogram")
    profile.add_argument("--elf", type=Path, required=True, help="echo_device.elf of the build that ran")
    profile.add_argument("--nm", help=f"the nm to run on the ELF (default {ARM_NM})")
    profile.add_argument("--top", type=int, default=DEFAULT_TOP, help="how many rows to list")
    return parser


def dispatch(tool, arguments):
    if arguments.command == "status":
        return tool.status()
    if arguments.command == "report":
        return tool.report()
    return tool.profile(arguments.elf, arguments.nm, arguments.top)


def main(argv=None, host=None):
    arguments = build_parser().parse_args(argv)
    tool = Tool(host, arguments.volume, arguments.volume_name)
    try:
        result = dispatch(tool, arguments)
    except DebugError as error:
        print(json.dumps(error.as_json()))
        return 1
    except OSError as error:
        print(json.dumps(DebugError("io_error", str(error)).as_json()))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
