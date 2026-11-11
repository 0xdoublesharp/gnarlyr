import argparse
import json
import math
import re
import struct
from pathlib import Path

RECORD = struct.Struct("<6I")
RECORDS = 2048
MAGIC = 0x44534254
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho")


def estimate(samples, capacity_mah=None):
    if capacity_mah is not None and (not math.isfinite(capacity_mah) or capacity_mah <= 0):
        raise ValueError("capacity must be a positive number of mAh")
    elapsed = samples[-1]["hours"] - samples[0]["hours"] if samples else 0
    mean_x = sum(item["hours"] for item in samples) / len(samples) if samples else 0
    mean_y = sum(item["percent"] for item in samples) / len(samples) if samples else 0
    variance = sum((item["hours"] - mean_x) ** 2 for item in samples)
    covariance = sum((item["hours"] - mean_x) * (item["percent"] - mean_y) for item in samples)
    slope = covariance / variance if variance else 0
    draining = slope < 0 and all(not item.get("usb_powered", False) for item in samples)
    return {
        "samples": samples, "elapsed_hours": elapsed,
        "percent_per_hour": -slope if draining else None,
        "hours_to_empty": samples[-1]["percent"] / -slope if draining else None,
        "full_discharge_hours": 100 / -slope if draining else None,
        "capacity_mah": capacity_mah,
        "ma_equivalent": capacity_mah * -slope / 100 if draining and capacity_mah else None,
        "basis": "linear fit of voltage-derived percent; current is capacity-scaled, not measured",
    }


def analyse(data, capacity_mah=None):
    samples = []
    previous_ms, elapsed_ms = 0, 0
    for offset in range(0, len(data) - RECORD.size + 1, RECORD.size):
        words = RECORD.unpack_from(data, offset)
        if not any(words):
            break
        checksum = MAGIC
        for word in words:
            checksum ^= word
        uptime, idle_us, clocks, battery, sequence, _ = words
        mv, percent, flags = battery & 65535, battery >> 16 & 255, battery >> 24
        if checksum or sequence != len(samples) + 1 or percent > 100 or not 2500 <= mv <= 5000:
            raise ValueError(f"invalid or torn sample at byte {offset}")
        delta = (uptime - previous_ms) % (1 << 32)
        if samples and (delta == 0 or delta > 86400000 or idle_us > delta * 1000):
            raise ValueError(f"invalid sample interval at byte {offset}")
        elapsed_ms += delta if samples else 0
        samples.append({"hours": elapsed_ms / 3600000, "uptime_ms": uptime,
                        "millivolts": mv, "percent": percent, "flags": flags,
                        "core0_clock_mhz": clocks & 65535, "core1_clock_mhz": clocks >> 16,
                        "core0_idle_us": idle_us,
                        "core0_busy_percent": 100 * (1 - idle_us / (delta * 1000)) if samples else None,
                        "usb_powered": bool(flags & 8)})
        previous_ms = uptime
    return estimate(samples, capacity_mah)


def analyse_report(text, capacity_mah=None):
    samples = []
    for line in text.splitlines():
        if not line.startswith("power.sample:"):
            continue
        for entry in line.split(":", 1)[1].split(";"):
            fields = entry.split()
            if len(fields) < 4:
                raise ValueError("invalid power.sample row")
            minute, mv, percent = map(int, fields[:3])
            samples.append({"hours": minute / 60, "millivolts": mv, "percent": percent,
                            "usb_powered": fields[3][3:4] == "1"})
    result = estimate(samples, capacity_mah)
    result["soaks"] = [line for line in text.splitlines() if line.startswith("script.soak:")]
    return result


def script(build, mode, minutes):
    if not re.fullmatch(r"[0-9a-f]{7,40}", build) or not 1 <= minutes <= 1440:
        raise ValueError("use a concrete build tag and 1 to 1440 minutes")
    pause = "play\nwait 35000\n" if mode == "standby" else ""
    return f"build {build}\n{pause}supply off\nscreen off\nsoak {minutes}\nreconnect\n"


def main():
    parser = argparse.ArgumentParser()
    actions = parser.add_subparsers(dest="action", required=True)
    prepare = actions.add_parser("prepare")
    prepare.add_argument("--out", type=Path, required=True)
    prepare.add_argument("--build", required=True)
    prepare.add_argument("--mode", choices=("playback", "standby"), default="playback")
    prepare.add_argument("--minutes", type=int, default=1440)
    analyse_command = actions.add_parser("analyse")
    analyse_command.add_argument("input", type=Path)
    analyse_command.add_argument("--capacity-mah", type=float)
    analyse_command.add_argument("--report", action="store_true")
    args = parser.parse_args()
    if args.action == "prepare":
        if not Path("/Volumes/Scratch").is_mount() or not args.out.resolve().is_relative_to(SCRATCH):
            parser.error("output must be on mounted Scratch under /Volumes/Scratch/SnowskyEcho")
        text = script(args.build, args.mode, args.minutes)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "battery.bin").write_bytes(bytes(RECORD.size * RECORDS))
        (args.out / "run.txt").write_text(text)
        result = {"trace": str(args.out / "battery.bin"), "script": str(args.out / "run.txt")}
    else:
        result = (analyse_report(args.input.read_text(), args.capacity_mah) if args.report
                  else analyse(args.input.read_bytes(), args.capacity_mah))
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
