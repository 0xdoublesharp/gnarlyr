import argparse
import math
from pathlib import Path

from audio_resample_table import bessel_zero
from fold_table import formatted


def cic_coefficients(ratio):
    step = 16 * ratio
    values = [1.0]
    for _ in range(5):
        expanded = [0.0] * (len(values) + step - 1)
        for index, value in enumerate(values):
            for offset in range(step):
                expanded[index + offset] += value / step
        values = expanded
    return values


def cic_response(frequency, ratio):
    step = 16 * ratio
    return 1.0 if frequency == 0 else (math.sin(math.pi * frequency)
                                       / (step * math.sin(math.pi * frequency / step))) ** 5


def compensation(ratio):
    taps, cutoff, points = 64, 0.17, 2048
    frequencies = [index * cutoff / points for index in range(points + 1)]
    inverse = [(0.5 if index in (0, points) else 1) / cic_response(value, ratio)
               for index, value in enumerate(frequencies)]
    values = []
    for index in range(taps):
        distance = index - (taps - 1) / 2
        integral = sum(weight * math.cos(2 * math.pi * frequency * distance)
                       for frequency, weight in zip(frequencies, inverse)) * 2 * cutoff / points
        window = bessel_zero(8.6 * math.sqrt(max(0, 1 - (distance / 31.5) ** 2))) / bessel_zero(8.6)
        values.append(integral * window)
    total = sum(values)
    return [value / total for value in values]


def quantize(values, bits):
    fixed = [round(value * (1 << bits)) for value in values]
    centre = len(fixed) // 2
    remainder = (1 << bits) - sum(fixed)
    if len(fixed) % 2:
        fixed[centre] += remainder
    else:
        assert remainder % 2 == 0
        fixed[centre - 1] += remainder // 2
        fixed[centre] += remainder // 2
    assert fixed == fixed[::-1]
    assert sum(abs(value) for value in fixed) < 2 * (1 << bits)
    return fixed


def response(values, frequency):
    centre = (len(values) - 1) / 2
    return sum(value * math.cos(2 * math.pi * frequency * (index - centre))
               for index, value in enumerate(values))


def render():
    lines = ["#pragma once", "", "#include <stdint.h>", ""]
    for ratio in (1, 2, 4):
        shift = {1: 20, 2: 25, 4: 30}[ratio]
        cic = [round(value * (1 << shift)) for value in cic_coefficients(ratio)]
        lines.append(f"static const int32_t DSD_CIC_{ratio}[{len(cic)}] = {{")
        for start in range(0, len(cic), 8):
            lines.append("    " + ", ".join(map(str, cic[start:start + 8])) + ",")
        lines.extend(["};", ""])
        values = quantize(compensation(ratio), 30)
        lines.append(f"static const int32_t DSD_SECOND_{ratio}[64] = {{")
        for start in range(0, 64, 8):
            lines.append("    " + ", ".join(map(str, values[start:start + 8])) + ",")
        lines.extend(["};", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--style", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    text = formatted(render(), arguments.style)
    if arguments.check:
        return 0 if arguments.output.read_text() == text else 1
    arguments.output.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
