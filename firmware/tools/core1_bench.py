"""Count core 1 decoder instructions per second of audio in the emulator.

Each case decodes a checked-in fixture with the core 1 program from a `device` build, in the same
way as the `core1_*_decodes_in_emulator` tests, and reports emulated instructions per second of
output audio. The figure counts every instruction as one cycle, so it is a relative measure of
decoder work, not a cycle-accurate time. Stream cases count from the first published block to the
last, which leaves out start-up. FLAC runs the program's whole-file decode command and subtracts a
run without input.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from echo_emulator import core1

FIXTURES = ROOT / "tests" / "fixtures"
DEFAULT_DEVICE = ROOT / "build" / "device" / "platform" / "device"
DEFAULT_TOOLS = ROOT / "build" / "dev" / "tests"
OGG_END = struct.pack("<IIQ", 0, 4, (1 << 64) - 1)
SHARED = 0x01020000
INPUT_BASE = 0x0102E000
INPUT_BYTES = 0x32000
COMMAND_BENCH = 0
COMMAND_DECODE = 1
FLAC_FIELDS = {"input_bytes": 0x10, "rate": 0x14, "channels": 0x18, "bits": 0x1C,
               "min_block": 0x20, "max_block": 0x24, "total": 0x28}
FLAC_BLOCKS = 0x30
FLAC_RESULT = 0x3C
DEFAULT_MEDIA = Path("/Volumes/Scratch/SnowskyEcho/tmp/decoders/media")
CASES = {
    "flac": [("media", "flac16.flac", None), ("media", "flac16_p12.flac", None),
             ("media", "flac16_fixed.flac", None), ("media", "flac24.flac", None)],
    "mp3": [("media", "mp3_128.mp3", None), ("media", "mp3_320.mp3", None)],
    "aac": [("media", "aac.m4a", None)],
    "alac": [("media", "alac.m4a", None), ("media", "alac_apple.m4a", None)],
    "vorbis": [("fixtures", "vorbis/seek44.ogg", 2), ("fixtures", "vorbis/stereo48.ogg", 2)],
    "opus": [("media", "opus.opus", 2), ("fixtures", "opus/speech16.opus", 1)],
}
MEDIA_COMMANDS = {
    "flac16.flac": ["-t", "1.5", "-c:a", "flac", "-compression_level", "5", "-frame_size", "4096"],
    "flac16_p12.flac": ["-t", "1.5", "-c:a", "flac", "-compression_level", "5", "-frame_size", "4096",
                        "-lpc_coeff_precision", "12"],
    "flac16_fixed.flac": ["-t", "1.5", "-c:a", "flac", "-compression_level", "0", "-frame_size",
                          "4096"],
    "flac24.flac": ["-t", "0.5", "-ar", "96000", "-c:a", "flac", "-sample_fmt", "s32",
                    "-bits_per_raw_sample", "24", "-compression_level", "5",
                    "-frame_size", "4096"],
    "mp3_128.mp3": ["-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0", "-write_xing", "0"],
    "mp3_320.mp3": ["-c:a", "libmp3lame", "-b:a", "320k", "-id3v2_version", "0", "-write_xing", "0"],
    "aac.m4a": ["-c:a", "aac", "-b:a", "192k"],
    "alac.m4a": ["-t", "4", "-c:a", "alac"],
    "alac_apple.m4a": ["-t", "4", "-c:a", "alac_at"],
    "opus.opus": ["-ar", "48000", "-c:a", "libopus", "-b:a", "128k", "-frame_duration", "20"],
}


def run_tool(tool, *arguments):
    with tempfile.TemporaryDirectory() as scratch:
        output = Path(scratch) / "out.bin"
        run = subprocess.run([str(tool), *arguments, str(output)], capture_output=True,
                             text=True, check=True)
        return run.stdout, output.read_bytes()


def steady_rate(blocks, rate):
    ready = [block for block in blocks if block[0] == core1.RESULT_READY]
    if len(ready) < 3:
        raise RuntimeError("too few blocks to time")
    frames = sum(block[2] for block in ready[1:])
    instructions = ready[-1][4] - ready[0][4]
    return instructions * rate / frames, frames / rate


def stream_decode(program, stream, channels, bits, first=0, end=None, tail=0, ring=65536):
    machine = core1.Core1(program, ring)
    if not machine.boot():
        raise RuntimeError("core 1 did not report ready")
    machine.start_stream(channels, bits, first)
    if end is not None:
        machine.set_control("end", end)
        machine.set_control("tail_trim", tail)
    fed = 0
    blocks = []
    for _ in range(4_000_000):
        fed = machine.feed(stream, fed)
        machine.run_slice()
        for block in machine.take_slots(channels):
            blocks.append(block)
            if block[0] != core1.RESULT_READY:
                return blocks
    raise RuntimeError("the stream did not finish")


def fixture_relative(path):
    return os.path.relpath(path, FIXTURES)


def bench_mp3(paths, path, _):
    stdout, _pcm = run_tool(paths.tools / "mp3_decode_tool", fixture_relative(path), "0")
    channels, rate, first, start, end, tail = (int(value) for value in stdout.split())
    encoded = path.read_bytes()[start:end]
    blocks = stream_decode(paths.program("core1_mp3"), encoded, channels, 24, first,
                           len(encoded), tail)
    return steady_rate(blocks, rate)


def bench_m4a(paths, path, _):
    stdout, stream = run_tool(paths.tools / "m4a_stream_tool", fixture_relative(path), "0")
    codec, channels, rate, _frame, _delay, _valid = (int(item) for item in stdout.split())
    bits = 24 if codec == 1 else 16
    blocks = stream_decode(paths.program("core1_m4a"), stream + bytes(65536), channels, bits)
    return steady_rate(blocks, rate)


def ogg_records(paths, path):
    _, data = run_tool(paths.tools / "ogg_packet_tool", str(path))
    found = []
    offset = 0
    while offset < len(data):
        size, flags, granule = struct.unpack_from("<IIQ", data, offset)
        found.append((flags, granule, data[offset:offset + 16 + size]))
        offset += 16 + size
    return found


def bench_vorbis(paths, path, channels):
    stream = b"".join(record[2] for record in ogg_records(paths, path)) + OGG_END
    rate = struct.unpack_from("<I", path.read_bytes(), 40)[0]
    blocks = stream_decode(paths.program("core1_vorbis"), stream, channels, 24)
    return steady_rate(blocks, rate)


def bench_opus(paths, path, channels):
    records = ogg_records(paths, path)
    stream = b"".join(record[2] for index, record in enumerate(records) if index != 1) + OGG_END
    blocks = stream_decode(paths.program("core1_opus"), stream, channels, 24, ring=40960)
    return steady_rate(blocks, 48000)


def flac_header(data):
    offset = 4
    info = None
    while True:
        header = data[offset]
        length = int.from_bytes(data[offset + 1:offset + 4], "big")
        body = data[offset + 4:offset + 4 + length]
        if header & 0x7F == 0:
            packed = int.from_bytes(body[10:18], "big")
            info = {"min_block": int.from_bytes(body[0:2], "big"),
                    "max_block": int.from_bytes(body[2:4], "big"),
                    "rate": packed >> 44, "channels": (packed >> 41 & 7) + 1,
                    "bits": (packed >> 36 & 31) + 1, "total": packed & (1 << 36) - 1}
        offset += 4 + length
        if header & 0x80:
            return info, data[offset:]


def flac_run(program, command, info=None, frames=b""):
    machine = core1.Core1(program, 0x10000)
    machine.put(SHARED + 0x0C, command)
    if info is not None:
        machine.uc.mem_write(INPUT_BASE, frames)
        for field, offset in FLAC_FIELDS.items():
            machine.put(SHARED + offset, len(frames) if field == "input_bytes" else info[field])
    for _ in range(4_000_000):
        machine.run_slice()
        if machine.word(SHARED) == core1.READY_MAGIC:
            return machine
    raise RuntimeError("core 1 never reported ready")


def bench_flac(paths, path, _):
    info, frames = flac_header(path.read_bytes())
    if len(frames) > INPUT_BYTES:
        raise RuntimeError("the FLAC fixture does not fit core 1's input region")
    program = paths.program("core1")
    idle = flac_run(program, COMMAND_BENCH).instructions
    machine = flac_run(program, COMMAND_DECODE, info, frames)
    if machine.word(SHARED + FLAC_BLOCKS) == 0:
        raise RuntimeError("no FLAC frame decoded")
    seconds = info["total"] / info["rate"]
    return (machine.instructions - idle) / seconds, seconds


BENCHES = {"flac": bench_flac, "mp3": bench_mp3, "aac": bench_m4a, "alac": bench_m4a,
           "vorbis": bench_vorbis, "opus": bench_opus}


class Paths:
    def __init__(self, device, tools, media):
        self.device = Path(device)
        self.tools = Path(tools)
        self.media = Path(media)

    def case(self, place, name):
        return (self.media if place == "media" else FIXTURES) / name

    def program(self, name):
        return (self.device / f"{name}.bin").read_bytes()


def make_media(source, media):
    media.mkdir(parents=True, exist_ok=True)
    clip = media / "source44.wav"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "10", "-t", "6",
                    "-i", str(source), "-ar", "44100", "-ac", "2", "-c:a", "pcm_s16le", str(clip)],
                   check=True)
    for name, options in MEDIA_COMMANDS.items():
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(clip),
                        *options, str(media / name)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--device", default=str(DEFAULT_DEVICE))
    parser.add_argument("--tools", default=str(DEFAULT_TOOLS))
    parser.add_argument("--media", default=str(DEFAULT_MEDIA))
    parser.add_argument("--make-media", metavar="SOURCE")
    parser.add_argument("--format", action="append", choices=sorted(CASES))
    parser.add_argument("--json")
    arguments = parser.parse_args()
    paths = Paths(arguments.device, arguments.tools, arguments.media)
    if arguments.make_media:
        make_media(Path(arguments.make_media), paths.media)
    results = {}
    for kind in arguments.format or list(CASES):
        for place, name, channels in CASES[kind]:
            per_second, seconds = BENCHES[kind](paths, paths.case(place, name), channels)
            results[f"{kind}/{Path(name).stem}"] = round(per_second)
            print(f"{kind:7} {Path(name).stem:28} {per_second / 1e6:8.2f} M instructions per audio "
                  f"second ({seconds:.2f} s timed)", flush=True)
    if arguments.json:
        Path(arguments.json).write_text(json.dumps(results, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
