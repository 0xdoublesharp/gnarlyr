import argparse
import math
from pathlib import Path

from fold_table import formatted

PHASES = (0, 2, 3, 4, 6, 8, 9, 10)
LEFT = 23
RIGHT = 24
FRACTION_BITS = 30
CUTOFF = 0.475
BETA = 8.6


def bessel_zero(value):
    total, term = 1.0, 1.0
    for index in range(1, 80):
        term *= value * value / (4 * index * index)
        total += term
        if term < total * 1e-17:
            break
    return total


def coefficients(phase):
    row = []
    for index in range(-LEFT, RIGHT + 1):
        distance = index - phase / 12
        argument = 2 * CUTOFF * distance
        sinc = math.sin(math.pi * argument) / (math.pi * argument) if argument else 1.0
        window = bessel_zero(BETA * math.sqrt(max(0, 1 - (distance / RIGHT) ** 2)))
        row.append(2 * CUTOFF * sinc * window / bessel_zero(BETA))
    scale = (1 << FRACTION_BITS) / sum(row)
    fixed = [round(value * scale) for value in row]
    peak = max(range(len(fixed)), key=lambda index: fixed[index])
    fixed[peak] += (1 << FRACTION_BITS) - sum(fixed)
    assert sum(abs(value) for value in fixed) < 4 * (1 << FRACTION_BITS)
    return fixed


def render():
    rows = ["#pragma once", "", "#include <stdint.h>", "",
            "static const int32_t RESAMPLE_COEFFICIENTS[8][48] = {"]
    for phase in PHASES:
        values = coefficients(phase)
        rows.append("    {")
        for at in range(0, len(values), 6):
            rows.append("        " + ", ".join(str(value) for value in values[at:at + 6]) + ",")
        rows.append("    },")
    rows.extend(["};", ""])
    return "\n".join(rows)


def main():
    parser = argparse.ArgumentParser(description="Generate fixed-point interpolation coefficients")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--style", type=Path, required=True)
    arguments = parser.parse_args()
    result = formatted(render(), arguments.style)
    if arguments.check:
        return 0 if arguments.output.read_text() == result else 1
    arguments.output.write_text(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
