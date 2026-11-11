import argparse
import base64
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import zlib

from vorbis_fixture import repair_page


def run(command, data=None):
    return subprocess.run(list(map(str, command)), input=data, capture_output=True, check=True).stdout


def helper(prefix, output, ogg=None):
    ogg = ogg or prefix
    path = output / (prefix.name + '-codec')
    run(['cc', '-std=c11', '-O2', Path(__file__).with_name('vorbis_fixture_codec.c'),
         '-I' + str(prefix / 'include'), '-L' + str(prefix / 'lib'),
         '-I' + str(ogg / 'include'), '-L' + str(ogg / 'lib'),
         '-Wl,-rpath,' + str(prefix / 'lib'), '-Wl,-rpath,' + str(ogg / 'lib'), '-lvorbisenc', '-lvorbisfile', '-lvorbis', '-logg', '-lm', '-o', path])
    return path


def signal(rate):
    rng = random.Random(42)
    values = []
    for frame in range(rate * 13 // 4):
        t = frame / rate
        for channel in range(2):
            phase = (997 if channel == 0 else 2111) * t + 29 * t * t
            tone = math.sin(2 * math.pi * phase) + 0.3 * math.sin(2 * math.pi * 73 * t)
            transient = 1 if frame % (rate // 7) < rate // 80 else 0.15
            values.append(int(7000 * tone + 5000 * transient * rng.uniform(-1, 1)))
    return struct.pack(f'<{len(values)}h', *values)


def pages(encoded):
    offset = 0
    while offset < len(encoded):
        count = encoded[offset + 26]
        size = 27 + count + sum(encoded[offset + 27:offset + 27 + count])
        yield encoded[offset:offset + size]
        offset += size


def other_page(sequence, flags):
    data = bytearray(b'OggS' + bytes([0, flags]) + struct.pack('<QIII', 0, 77, sequence, 0) + bytes([1, 5]) + b'other')
    repair_page(data, 0, len(data))
    return bytes(data)


def picture(output):
    rng = random.Random(99)
    pixels = rng.randbytes(600 * 600 * 3)
    raw = b''.join(b'\0' + pixels[row * 1800:(row + 1) * 1800] for row in range(600))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 600, 600, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    block = struct.pack('>II', 3, 9) + b'image/png' + struct.pack('>IIIIII', 0, 600, 600, 24, 0, len(png)) + png
    path = output / 'picture.base64'
    path.write_bytes(base64.b64encode(block))
    return path


def generate(args):
    codec = helper(args.prefix, args.output)
    aotuv = helper(args.aotuv, args.output, args.prefix) if args.aotuv else None
    entries = []
    def save(name, encoded, reference=None, refused=False):
        (args.output / (name + '.ogg')).write_bytes(encoded)
        pcm = b'' if refused else run([codec, 'decode'], encoded if reference is None else reference)
        (args.output / (name + '.pcm')).write_bytes(pcm)
        entries.append({'file': name, 'refused': refused})
    def encode(name, rate, mode, encoder=codec, extra=()):
        save(name, run([encoder, 'encode', 2, rate, mode, *extra], signal(rate)))
    for rate in (44100, 48000):
        for quality in (-1, 0, 3, 5, 8, 10):
            encode(f'xiph-q{quality}-{rate}', rate, str(quality / 10))
    speech = args.output / 'speech.aiff'
    run(['say', '-o', speech, 'The quick brown fox jumps over the lazy dog. Music and speech should play clearly.'])
    for rate in (8000, 16000, 22050):
        raw = run(['ffmpeg', '-v', 'error', '-i', speech, '-t', '3.25', '-ar', rate, '-ac', 1, '-f', 's16le', '-'])
        save(f'speech-{rate}', run([codec, 'encode', 1, rate, '0.4'], raw))
    for rate in (88200, 96000, 192000):
        encode(f'xiph-q10-{rate}', rate, '1.0')
    for mode in ('cbr:128000', 'abr:192000', 'cbr:500000'):
        encode(mode.replace(':', '-') + '-48000', 48000, mode)
    for rate in (44100, 48000):
        raw = signal(rate)
        encoded = run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', rate, '-ac', 2, '-i', '-',
                       '-c:a', 'vorbis', '-strict', 'experimental', '-q:a', 5, '-f', 'ogg', '-'], raw)
        save(f'ffmpeg-native-{rate}', encoded)
    encode('large-picture', 44100, '0.5', extra=(picture(args.output),))
    first = (args.output / 'xiph-q5-44100.ogg').read_bytes()
    second = bytearray((args.output / 'xiph-q3-48000.ogg').read_bytes())
    offset = 0
    for page in pages(second):
        struct.pack_into('<I', second, offset + 14, 43)
        repair_page(second, offset, len(page))
        offset += len(page)
    save('chained', first + second, reference=first)
    original = list(pages(first))
    multiplexed = original[0] + other_page(0, 2) + b''.join(original[1:-1]) + other_page(1, 4) + original[-1]
    save('multiplexed', multiplexed, reference=first)
    save('ogg-flac', run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', 48000, '-ac', 2, '-i', '-',
                          '-c:a', 'flac', '-f', 'ogg', '-'], signal(48000)), refused=True)
    if aotuv:
        for quality in (-1, 3, 10):
            encode(f'aotuv-q{quality}-48000', 48000, str(quality / 10), encoder=aotuv)
    (args.output / 'manifest.json').write_text(json.dumps(entries, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description='Exercise independent Vorbis encoders through the firmware file and decoder paths')
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--aotuv', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--harness', type=Path, required=True)
    parser.add_argument('--run-only', action='store_true')
    args = parser.parse_args()
    scratch = Path('/Volumes/Scratch')
    if not scratch.is_mount() or not args.output.resolve().is_relative_to(scratch):
        parser.error('output must be on the mounted Scratch volume')
    args.output.mkdir(parents=True, exist_ok=True)
    os.environ['TMPDIR'] = str(args.output)
    if not args.run_only:
        generate(args)
    results = []
    for entry in json.loads((args.output / 'manifest.json').read_text()):
        path = args.output / entry['file']
        process = subprocess.run([str(args.harness), str(path) + '.ogg', str(path) + '.pcm'], capture_output=True, text=True)
        result = entry | json.loads(process.stdout)
        result['success'] = (not result['probe']) if entry['refused'] else result['passed']
        results.append(result)
        print(f"{entry['file']:28} {'PASS' if result['success'] else 'FAIL':4} {result['workspace_peak']:6} {result['max_packet']:7} {result['max_difference']:3}", flush=True)
    (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(item['success'] for item in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
