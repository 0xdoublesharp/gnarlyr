import argparse
from pathlib import Path

from fold_table import formatted
from replay_gain import gain_powers


def render():
    rows = ["#pragma once", "", "#include <stdint.h>", "",
            "static const uint64_t REPLAY_GAIN_POWERS[2][14] = {"]
    for factors in gain_powers():
        rows.append("    {")
        rows.extend(f"        UINT64_C({fixed})," for fixed in factors)
        rows.append("    },")
    rows.extend(["};", ""])
    return "\n".join(rows)


def main():
    parser = argparse.ArgumentParser(description="Generate fixed-point ReplayGain factors")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--style", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    result = formatted(render(), arguments.style)
    if arguments.check:
        return 0 if arguments.output.read_text() == result else 1
    arguments.output.write_text(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
