import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

from echo_emulator import core1


def measure(program, records, rate, reference):
    machine = core1.Core1(program, 65536)
    machine.uc.mem_write(0x0102A000, b'\xa5' * 0x4000)
    if not machine.boot():
        raise RuntimeError('Vorbis program did not report ready')
    machine.start_stream(2, 24, 0)
    records += struct.pack('<IIQ', 0, 4, (1 << 64) - 1)
    fed = 0
    frames = 0
    maximum_difference = 0
    reference = struct.unpack(f"<{len(reference) // 2}h", reference)
    first_audio = None
    first_frames = 0
    for _ in range(2000000):
        fed = machine.feed(records, fed)
        machine.run_slice()
        for result, first, count, data, instructions in machine.take_slots(2):
            if result == core1.RESULT_READY:
                if first_audio is None and count:
                    first_audio = instructions
                    first_frames = count
                if first != frames:
                    raise RuntimeError('noncontiguous Vorbis output')
                for frame in range(count):
                    for channel in range(2):
                        difference = abs((data[channel][frame] >> 8) - reference[(frames + frame) * 2 + channel])
                        maximum_difference = max(maximum_difference, difference)
                frames += count
            elif result == core1.RESULT_END:
                if machine.uc.mem_read(0x0102A000, 64) != b'\xa5' * 64:
                    raise RuntimeError('Vorbis crossed the stack guard')
                if frames * 2 != len(reference) or maximum_difference > 2:
                    raise RuntimeError('ARM Vorbis output differs from the reference')
                return {'frames': frames, 'max_difference': maximum_difference, 'instructions': instructions,
                        'per_20ms_including_setup': round(instructions * rate / frames / 50),
                        'per_20ms_audio': round((instructions - first_audio) * rate / (frames - first_frames) / 50)}
            else:
                raise RuntimeError(f'Vorbis decode failed: {result}')
    raise RuntimeError('Vorbis decode did not finish')


def main():
    parser = argparse.ArgumentParser(description='Count Cortex-M3 Vorbis instructions through the actual core-1 program')
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--tool', type=Path, required=True)
    args = parser.parse_args()
    scratch = Path('/Volumes/Scratch')
    if not scratch.is_mount() or not args.corpus.resolve().is_relative_to(scratch):
        parser.error('corpus must be on the mounted Scratch volume')
    os.environ['TMPDIR'] = str(args.corpus)
    results = []
    for name, rate in [('xiph-q10-48000', 48000), ('xiph-q10-96000', 96000),
                       ('xiph-q10-192000', 192000), ('cbr-500000-48000', 48000)]:
        records = args.corpus / (name + '.records')
        subprocess.run([str(args.tool), str(args.corpus / (name + '.ogg')), str(records)], check=True)
        result = {'file': name, 'rate': rate} | measure(args.program.read_bytes(), records.read_bytes(), rate, (args.corpus / (name + '.pcm')).read_bytes())
        results.append(result)
        print(json.dumps(result), flush=True)
    (args.corpus / 'headroom.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
