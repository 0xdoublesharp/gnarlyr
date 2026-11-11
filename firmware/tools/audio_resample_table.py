import argparse
import math
from pathlib import Path

from fold_table import formatted

PAIRED = (2, 3, 4)
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
    peaks = [index for index, value in enumerate(fixed) if value == max(fixed)]
    remainder = (1 << FRACTION_BITS) - sum(fixed)
    for peak in peaks:
        fixed[peak] += remainder // len(peaks)
    assert sum(fixed) == 1 << FRACTION_BITS
    assert sum(abs(value) for value in fixed) < 4 * (1 << FRACTION_BITS)
    return fixed


def folded():
    taps = LEFT + RIGHT + 1
    centre, half = coefficients(0), coefficients(6)
    assert centre[:taps - 1] == centre[taps - 2::-1] and half == half[::-1]
    tables = {"RESAMPLE_CENTRE": centre[:LEFT + 1] + [centre[taps - 1]],
              "RESAMPLE_HALF": half[:taps // 2]}
    sums, differences = [], []
    for phase in PAIRED:
        row = coefficients(phase)
        assert coefficients(12 - phase) == row[::-1]
        sums.append([row[index] + row[taps - 1 - index] for index in range(taps // 2)])
        differences.append([row[index] - row[taps - 1 - index] for index in range(taps // 2)])
    assert all(abs(value) < 1 << 31 for row in sums + differences for value in row)
    return tables, {"RESAMPLE_SUM": sums, "RESAMPLE_DIFFERENCE": differences}


def table_lines(values):
    return ["        " + ", ".join(str(value) for value in values[at:at + 6]) + ","
            for at in range(0, len(values), 6)]


def render():
    single, paired = folded()
    rows = ["#pragma once", "", "#include <stdint.h>", ""]
    for name, values in single.items():
        rows.append(f"static const int32_t {name}[{len(values)}] = {{")
        rows.extend(line[4:] for line in table_lines(values))
        rows.extend(["};", ""])
    for name, table in paired.items():
        rows.append(f"static const int32_t {name}[{len(table)}][{len(table[0])}] = {{")
        for values in table:
            rows.append("    {")
            rows.extend(table_lines(values))
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
