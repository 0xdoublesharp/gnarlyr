import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import art_codec
import art_fixtures

BASELINE = TOOLS / "art_codec_bench.json"
CODE_BASE = 0x00100000
DATA_BASE = 0x20000000
DATA_BYTES = 0x00400000
STACK_TOP = DATA_BASE + DATA_BYTES
STOP = 0x00F00000
SPANS = {"full": None, "marquee": (136, 466), "partial": (300, 400)}
CODECS = (("indexed", art_codec.INDEXED, "fs"), ("lossless", art_codec.LOSSLESS, "none"))
FLAGS = ["-mcpu=cortex-m3", "-mthumb", "-std=c11", "-Os", "-ffreestanding", "-fno-builtin",
         "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror"]
TOLERANCE = 1.10


def compile_bench(directory):
    compiler = shutil.which("arm-none-eabi-gcc")
    if compiler is None:
        raise SystemExit("arm-none-eabi-gcc is not on PATH")
    sources = [TOOLS / "art_codec_bench.c", ROOT / "src/gfx/art_codec.c", ROOT / "src/gfx/color.c"]
    elf = Path(directory) / "bench.elf"
    subprocess.run([compiler, *FLAGS, "-I", str(ROOT / "src/gfx/include"), "-nostdlib",
                    "-Wl,--gc-sections", "-Wl,-e,bench_run", f"-Wl,-Ttext=0x{CODE_BASE:x}",
                    *map(str, sources), "-o", str(elf)], check=True)
    sizes = subprocess.run([shutil.which("arm-none-eabi-nm"), "-S", "--size-sort", str(elf)],
                           capture_output=True, text=True, check=True).stdout
    symbols = {}
    for line in sizes.splitlines():
        address, size, kind, name = line.split()
        symbols[name] = (int(address, 16), int(size, 16))
    objcopy = shutil.which("arm-none-eabi-objcopy")
    binary = Path(directory) / "bench.bin"
    subprocess.run([objcopy, "-O", "binary", str(elf), str(binary)], check=True)
    return binary.read_bytes(), symbols


class Bench:
    def __init__(self, code, symbols):
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(CODE_BASE, (len(code) + 0xFFFF) & ~0xFFFF)
        self.uc.mem_map(STOP & ~0xFFF, 0x1000)
        self.uc.mem_map(DATA_BASE, DATA_BYTES)
        self.uc.mem_write(CODE_BASE, code)
        self.entry = symbols["bench_run"][0]
        self.count = 0
        self.uc.hook_add(UC_HOOK_CODE, self.counted, begin=CODE_BASE, end=CODE_BASE + len(code))

    def counted(self, uc, address, size, user):
        self.count += 1

    def run(self, data, width, height, span):
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_SP
        info = art_codec.parse(data, width, height)
        first, last = span or (0, width)
        payload_at, table_at = DATA_BASE + 0x1000, DATA_BASE + 0x200000
        palette_at, out_at, job_at = DATA_BASE + 0x300000, DATA_BASE + 0x310000, DATA_BASE
        colors = (info["colors"] or []) + [0] * (art_codec.PALETTE_MAX - len(info["colors"] or []))
        nibbles = bytearray(128)
        for index, alpha in enumerate(info["alphas"] or []):
            if index < info["alpha"]:
                nibbles[index >> 1] |= alpha << (4 * (index & 1))
        alpha_entries = info["alpha"] if info["codec"] == art_codec.INDEXED else 0
        self.uc.mem_write(payload_at, data)
        self.uc.mem_write(table_at, struct.pack("<%dI" % len(info["table"]), *info["table"]))
        self.uc.mem_write(palette_at, struct.pack("<256H", *colors) + bytes(nibbles) +
                          struct.pack("<II", alpha_entries, len(info["colors"] or [])))
        self.uc.mem_write(job_at, struct.pack("<4I6i", payload_at, table_at, palette_at, out_at,
                                              width, height, first, last, info["codec"],
                                              info["alpha"]))
        self.uc.reg_write(UC_ARM_REG_R0, job_at)
        self.uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        self.uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        self.count = 0
        self.uc.emu_start(self.entry | 1, STOP)
        if self.uc.reg_read(UC_ARM_REG_R0) != 0:
            raise RuntimeError("the benchmark decode failed")
        return self.count / (height * (last - first))


def measure(bench, image):
    results = {}
    for name, codec, dither in CODECS:
        try:
            pixels = art_codec.prepared(image, codec, dither)
            data = art_codec.payload(pixels, codec)
        except ValueError as error:
            results[name] = {"error": str(error)}
            continue
        raw = image.width * image.height * 2
        entry = {"bytes": len(data), "ratio": round(raw / len(data), 2)}
        for span, limits in SPANS.items():
            if limits is None or limits[1] <= image.width:
                entry[span] = round(bench.run(data, image.width, image.height, limits), 2)
        results[name] = entry
    return results


def fixtures():
    return {name: make(480, 222) for name, make in art_fixtures.FIXTURES.items()}


def corpus(folder):
    from PIL import Image, ImageOps
    images = {}
    for path in sorted(Path(folder).iterdir()):
        if path.suffix.lower() in (".png", ".jpg", ".jpeg"):
            with Image.open(path) as image:
                image = image.convert("RGBA" if "A" in image.mode else "RGB")
                kept = image.size in ((480, 222), (222, 480))
                images[path.stem] = image if kept else ImageOps.fit(image, (480, 222))
    return images


def regressions(results, baseline):
    failures = []
    for fixture, codecs in baseline.items():
        for codec, spans in codecs.items():
            for span, value in spans.items():
                if span in SPANS and results[fixture][codec][span] > value * TOLERANCE:
                    failures.append(f"{fixture} {codec} {span}: {results[fixture][codec][span]}"
                                    f" > {value} + 10 %")
    return failures


def main():
    parser = argparse.ArgumentParser(description="ARM instructions per decoded pixel for theme art")
    parser.add_argument("--check", action="store_true", help="fail on a regression over 10 percent")
    parser.add_argument("--write", action="store_true", help="record the fixture figures")
    parser.add_argument("--corpus", type=Path, help="also measure every PNG and JPEG here")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        code, symbols = compile_bench(directory)
    bench = Bench(code, symbols)
    sizes = {name: symbols[name][1] for name in ("art_row_indexed", "art_row_lossless",
                                                  "art_row_indices_below") if name in symbols}
    results = {name: measure(bench, image) for name, image in fixtures().items()}
    report = {"code_bytes": sizes, "fixtures": results}
    if args.corpus:
        report["corpus"] = {name: measure(bench, image) for name, image in corpus(args.corpus).items()}
    print(json.dumps(report, indent=2))
    if args.write:
        BASELINE.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    if args.check:
        failures = regressions(results, json.loads(BASELINE.read_text()))
        if failures:
            raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
