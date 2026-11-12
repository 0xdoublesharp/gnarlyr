import argparse
import re
import subprocess
from pathlib import Path


def signed(value):
    return (value + 2**31) % 2**32 - 2**31


def sine(phase, angles):
    x, y = signed(0xB2458939), 0
    z = phase
    if z < 0x3FFFFFFF:
        x = signed(-x)
        z += 0x3FFFFFFF
    elif z < 3 * 0x3FFFFFFF:
        z -= 0x3FFFFFFF
    else:
        x = signed(-x)
        z -= 3 * 0x3FFFFFFF
    for index in range(31):
        x1, y1 = x >> index, y >> index
        if z >= 0x3FFFFFFF:
            x, y, z = signed(x - y1), signed(y + x1), (z - angles[index]) % 2**32
        else:
            x, y, z = signed(x + y1), signed(y - x1), (z + angles[index]) % 2**32
    return y


def generate(source):
    table = source.read_text().split('atan_table[] = {', 1)[1].split('};', 1)[0]
    angles = [int(value, 16) for value in re.findall(r'0x[0-9a-fA-F]+', table)]
    lines = ['#pragma once', '', '#include <stdint.h>', '']
    for power in range(7, 12):
        count = 1 << power
        alpha = (1 << 15) >> (power + 1)
        values = [sine(((index * 65536 + 32768) * alpha >> 16) << 16, angles)
                  for index in range(count)]
        lines.append(f'static const int32_t wma_window_{count}[{count}] = {{')
        for index in range(0, count, 8):
            lines.append('    ' + ', '.join(map(str, values[index:index + 8])) + ',')
        lines += ['};', '']
    lines += ['static const int32_t *const wma_windows[] = {',
              '    wma_window_128, wma_window_256, wma_window_512, wma_window_1024, wma_window_2048,',
              '};', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--style', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / 'third_party/rockbox_wma/codecs/libwma/wmafixed.c'
    text = generate(source)
    text = subprocess.run(['clang-format', f'--style=file:{args.style.resolve()}'], input=text,
                          text=True, check=True, capture_output=True).stdout
    if args.check:
        if not args.output.exists() or args.output.read_text() != text:
            raise SystemExit('WMA windows are stale; run tools/firmware.py regen')
    else:
        args.output.write_text(text)


if __name__ == '__main__':
    main()
