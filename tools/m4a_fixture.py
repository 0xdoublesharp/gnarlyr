import argparse
import math
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

FFMPEG = Path.home() / ".local" / "bin" / "ffmpeg"
FRAMES = 1.0
GAPLESS_DELAY = 2112
GAPLESS_PADDING = 700

TRACKS = [
    {"folder": "Aac Artist/Aac Album", "file": "01 Lead.m4a", "codec": "aac", "rate": 44100,
     "bits": 16, "channels": 2, "faststart": True, "gapless": True, "cover": "jpeg",
     "title": "Lead", "artist": "Aac Artist", "number": 1},
    {"folder": "Aac Artist/Aac Album", "file": "02 Tail.m4a", "codec": "aac", "rate": 44100,
     "bits": 16, "channels": 2, "faststart": False, "gapless": False, "cover": None,
     "title": "Tail", "artist": "Aac Artist", "number": 2},
    {"folder": "Aac Artist/Aac Album", "file": "03 Mono.m4a", "codec": "aac", "rate": 48000,
     "bits": 16, "channels": 1, "faststart": True, "gapless": True, "cover": None,
     "title": "Mono", "artist": "Aac Artist", "number": 3},
    {"folder": "Alac Artist/Alac Album", "file": "01 Lossless.m4a", "codec": "alac", "rate": 44100,
     "bits": 16, "channels": 2, "faststart": True, "gapless": False, "cover": "png",
     "title": "Lossless", "artist": "Alac Artist", "number": 1},
    {"folder": "Alac Artist/Alac Album", "file": "02 Deep.m4a", "codec": "alac", "rate": 48000,
     "bits": 24, "channels": 2, "faststart": False, "gapless": False, "cover": None,
     "title": "Deep", "artist": "Alac Artist", "number": 2},
    {"folder": "Alac Artist/Alac Album", "file": "03 Single.m4a", "codec": "alac", "rate": 44100,
     "bits": 16, "channels": 1, "faststart": True, "gapless": False, "cover": None,
     "title": "Single", "artist": "Alac Artist", "number": 3},
]


class Noise:
    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return self.state / 0x7FFFFFFF * 2 - 1


def sample_at(index, channel, rate, noise):
    time = index / rate
    tone = math.sin(2 * math.pi * (440 + 220 * channel) * time)
    overtone = 0.3 * math.sin(2 * math.pi * 3150 * time + channel)
    envelope = 0.5 + 0.5 * math.sin(2 * math.pi * 1.3 * time)
    return (0.45 * tone + 0.2 * overtone) * envelope + 0.05 * noise.next()


def signal(track):
    rate, channels = track["rate"], track["channels"]
    peak = (1 << (track["bits"] - 1)) - 1
    noise = Noise(11 + rate % 97 + channels)
    count = int(FRAMES * rate)
    return [[int(sample_at(index, channel, rate, noise) * peak) for channel in range(channels)]
            for index in range(count)]


def write_wav(path, track, rows):
    width = track["bits"] // 8
    with wave.open(str(path), "wb") as output:
        output.setnchannels(track["channels"])
        output.setsampwidth(width)
        output.setframerate(track["rate"])
        packed = bytearray()
        for row in rows:
            for value in row:
                packed += value.to_bytes(4, "little", signed=True)[:width]
        output.writeframes(bytes(packed))


def ffmpeg(*arguments):
    subprocess.run([str(FFMPEG), "-y", "-v", "error", *arguments], check=True)


def encode(track, wav_path, output):
    codec = ["-c:a", "aac", "-b:a", "128k", "-aac_pns", "0"] if track["codec"] == "aac" else ["-c:a", "alac"]
    flags = ["-movflags", "+faststart"] if track["faststart"] else []
    ffmpeg("-i", str(wav_path), "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a",
           "+bitexact", *codec, *flags, str(output))


def cover_bytes(kind):
    from io import BytesIO
    from PIL import Image
    image = Image.new("RGB", (64, 64))
    image.putdata([(x * 4, y * 4, (x + y) * 2) for y in range(64) for x in range(64)])
    buffer = BytesIO()
    image.save(buffer, "JPEG" if kind == "jpeg" else "PNG")
    return buffer.getvalue()


def gapless_text(total_frames, delay, padding):
    valid = total_frames - delay - padding
    return f" 00000000 {delay:08X} {padding:08X} {valid:016X} " + " ".join(["00000000"] * 8)


def frame_count(path):
    from mutagen.mp4 import MP4
    info = MP4(path).info
    return round(info.length * info.sample_rate)


def tag(track, path):
    from mutagen.mp4 import MP4, MP4Cover, MP4FreeForm
    audio = MP4(path)
    audio.clear()
    audio["\xa9nam"] = track["title"]
    audio["\xa9ART"] = track["artist"]
    audio["aART"] = track["artist"]
    audio["\xa9alb"] = track["folder"].split("/")[1]
    audio["trkn"] = [(track["number"], 3)]
    audio["disk"] = [(1, 1)]
    audio["\xa9day"] = "2001"
    audio["\xa9gen"] = "Test"
    if track["cover"]:
        kind = MP4Cover.FORMAT_JPEG if track["cover"] == "jpeg" else MP4Cover.FORMAT_PNG
        audio["covr"] = [MP4Cover(cover_bytes(track["cover"]), imageformat=kind)]
    if track["gapless"]:
        text = gapless_text(frame_count(path), GAPLESS_DELAY, GAPLESS_PADDING)
        audio["----:com.apple.iTunes:iTunSMPB"] = [MP4FreeForm(text.encode("ascii"))]
    audio.save()


def reference(track, path, output):
    sample_format = "s16le" if track["bits"] == 16 else "s32le"
    ffmpeg("-ignore_editlist", "1", "-i", str(path), "-f", sample_format, "-acodec",
           "pcm_" + sample_format, str(output))


def make_track(track, music, references):
    folder = music / track["folder"]
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / track["file"]
    with tempfile.TemporaryDirectory() as scratch:
        wav_path = Path(scratch) / "in.wav"
        write_wav(wav_path, track, signal(track))
        encode(track, wav_path, path)
    tag(track, path)
    reference(track, path, references / (path.stem.split(" ", 1)[1].lower() + ".pcm"))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the M4A fixtures with ffmpeg and mutagen")
    parser.add_argument("--music", type=Path, required=True)
    parser.add_argument("--references", type=Path, required=True)
    arguments = parser.parse_args(argv)
    arguments.references.mkdir(parents=True, exist_ok=True)
    for track in TRACKS:
        make_track(track, arguments.music, arguments.references)
    return 0


if __name__ == "__main__":
    sys.exit(main())
