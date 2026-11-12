import argparse
import os
import struct
import subprocess
from pathlib import Path

TRACKS = (("v1mono", "wmav1", 16000, 1, "32k", "0.5", 3200),
          ("v2stereo", "wmav2", 48000, 2, "128k", "0.5", 3200),
          ("v2seek", "wmav2", 44100, 2, "96k", "2.0", 3200),
          ("v2fragment", "wmav2", 44100, 2, "96k", "0.5", 256))


def compressed_fixture(output, env):
    source = output / "v1mono.wma"
    payload = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(source),
                                       "-map", "0:a:0", "-c", "copy", "-f", "data", "-"], env=env)
    data = bytearray(source.read_bytes())
    packet_at = struct.unpack_from("<Q", data, 16)[0] + 50
    body = b"".join(bytes([128]) + payload[index:index + 128] for index in range(0, len(payload), 128))
    payload_header = struct.pack("<BBIBB", 1, 0, 3100, 1, 32)
    padding = 3200 - (13 + len(payload_header) + len(body))
    packet = b"\x82\x00\x00\x10\x5d" + struct.pack("<HIH", padding, 0, 512)
    packet += payload_header + body + bytes(padding)
    if len(packet) != 3200 or len(data) != packet_at + 3200:
        raise ValueError("the compressed-payload fixture must fit one ASF packet")
    data[packet_at:] = packet
    (output / "v1compressed.wma").write_bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(args.output))
    for name, codec, rate, channels, bitrate, seconds, packet_bytes in TRACKS:
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                        f"aevalsrc=0.25*sin(2*PI*(997*t+43*t*t))|0.2*sin(2*PI*2111*t):s={rate}",
                        "-t", seconds, "-ac", str(channels), "-packet_size", str(packet_bytes), "-c:a", codec, "-b:a", bitrate,
                        "-fflags", "+bitexact", "-flags:a", "+bitexact", "-map_metadata", "-1",
                        "-metadata", "title=Reference", "-metadata", "artist=Codec Fixture",
                        str(args.output / f"{name}.wma")], env=env, check=True)

    compressed_fixture(args.output, env)


if __name__ == "__main__":
    main()
