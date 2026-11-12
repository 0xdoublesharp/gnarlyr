import argparse
import re
import subprocess
import sys

import firmware

PREFLIGHT_TESTS = frozenset({"tools_unit_tests"})


def shard_names(names, shard, shards):
    if shards < 1 or shard < 1 or shard > shards:
        raise ValueError("shard must be between 1 and the shard count")
    if len(names) != len(set(names)):
        raise ValueError("CTest registry contains duplicate test names")
    selected = sorted(set(names) - PREFLIGHT_TESTS)[shard - 1::shards]
    if not selected:
        raise ValueError("shard selection is empty")
    return selected


def shard_regex(names, shard, shards):
    return "^(" + "|".join(re.escape(name) for name in shard_names(names, shard, shards)) + ")$"


def main():
    parser = argparse.ArgumentParser(description="Run a deterministic firmware CI shard with CTest fixtures")
    parser.add_argument("--shard", type=int, required=True)
    parser.add_argument("--shards", type=int, default=6)
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=4)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.shards < 1 or args.shard < 1 or args.shard > args.shards:
        parser.error("shard must be between 1 and the shard count")
    env, python = firmware.environment(args.jobs)
    firmware.configure("dev", env, python)
    names = [test["name"] for test in firmware.selected_tests(None, env)]
    if not PREFLIGHT_TESTS.issubset(names):
        raise ValueError("the required preflight test is missing from CTest")
    regex = shard_regex(names, args.shard, args.shards)
    selected = shard_names(names, args.shard, args.shards)
    print(f"Shard {args.shard}/{args.shards}: {len(selected)} tests before required fixtures", flush=True)
    command = [python, firmware.ROOT / "tools/firmware.py", "test", "--jobs", str(args.jobs), "--regex", regex]
    if args.list:
        command.append("--list")
    firmware.run(command, env)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
