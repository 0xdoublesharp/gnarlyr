import argparse
import decimal
from pathlib import Path

from fold_table import formatted


def render():
    rows = ["#pragma once", "", "#include <stdint.h>", "",
            "static const uint64_t REPLAY_GAIN_POWERS[2][14] = {"]
    with decimal.localcontext() as context:
        context.prec = 60
        for sign in (1, -1):
            rows.append("    {")
            for bit in range(14):
                exponent = decimal.Decimal(sign * (1 << bit)) / 2000
                value = decimal.Decimal(10) ** exponent * (1 << 32)
                fixed = int(value.to_integral_value(rounding=decimal.ROUND_HALF_EVEN))
                rows.append(f"        UINT64_C({fixed}),")
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
