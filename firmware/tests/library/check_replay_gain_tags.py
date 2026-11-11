import argparse
import math
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import mutagen
from mutagen.apev2 import APEv2
from mutagen.id3 import ID3, TXXX
from mutagen.mp4 import MP4

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import library_index

READER = None
FIXTURES = None
VALUES = {"REPLAYGAIN_TRACK_GAIN": "+6.00 dB", "REPLAYGAIN_ALBUM_GAIN": "-3.00 dB",
          "REPLAYGAIN_TRACK_PEAK": "1.25", "REPLAYGAIN_ALBUM_PEAK": "0.5"}
FORMATS = ("tagged.flac", "tagged.ogg", "id3v24.mp3", "aac.m4a", "alac.m4a",
           "tagged.wav", "tagged.wma", "tagged.ape", "tagged.dsf")


def write_tags(path, values, encoding=3, version=4, namespace="com.apple.iTunes"):
    audio = mutagen.File(path)
    if audio.tags is None:
        audio.add_tags()
    for key, value in values.items():
        if isinstance(audio.tags, ID3):
            audio.tags.add(TXXX(encoding=encoding, desc=key, text=[value]))
        elif isinstance(audio, MP4):
            audio.tags[f"----:{namespace}:{key}"] = [value.encode("utf-8")]
        else:
            audio.tags[key] = [value]
    if isinstance(audio.tags, ID3):
        audio.save(v2_version=version)
    else:
        audio.save()


def read_gains(path):
    output = subprocess.check_output([str(READER), str(path)], text=True)
    return tuple(int(value) for value in output.split())


class ReplayGainFormatsTest(unittest.TestCase):
    def assert_normalization(self, gains):
        self.assertEqual(4, len(gains))
        for actual, db in zip(gains[:2], (6, -3)):
            expected = math.pow(10, db / 20) * 65536
            self.assertAlmostEqual(actual / expected, 1, delta=0.0001)
        self.assertEqual(52428, gains[2])
        self.assertEqual(gains[1], gains[3])

    def assert_desktop_agrees(self, path):
        self.assertEqual(read_gains(path), library_index.read_track(path.parent, path).replay_gain)

    def test_standard_tags_in_real_supported_containers(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-tags-") as directory:
            for name in FORMATS:
                with self.subTest(name=name):
                    path = Path(directory) / name
                    shutil.copyfile(FIXTURES / name, path)
                    write_tags(path, VALUES)
                    self.assert_normalization(read_gains(path))
                    self.assert_desktop_agrees(path)

    def test_id3_text_encodings_and_versions(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-id3-") as directory:
            for encoding, version in ((0, 3), (1, 3), (1, 4), (2, 4), (3, 4)):
                with self.subTest(encoding=encoding, version=version):
                    path = Path(directory) / f"track-{encoding}-{version}.mp3"
                    shutil.copyfile(FIXTURES / "id3v24.mp3", path)
                    write_tags(path, VALUES, encoding=encoding, version=version)
                    self.assert_normalization(read_gains(path))
                    self.assert_desktop_agrees(path)

    def test_mp3_apev2_fallback_and_id3_precedence(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-ape-") as directory:
            path = Path(directory) / "track.mp3"
            shutil.copyfile(FIXTURES / "id3v24.mp3", path)
            ape = APEv2()
            for key, value in VALUES.items():
                ape[key] = value
            ape.save(path)
            self.assert_normalization(read_gains(path))
            self.assert_desktop_agrees(path)
            write_tags(path, {"REPLAYGAIN_TRACK_GAIN": "0 dB"})
            gains = read_gains(path)
            self.assertEqual(65536, gains[0])
            self.assertEqual(52428, gains[2])
            self.assert_desktop_agrees(path)

    def test_invalid_id3_encoding_and_overlong_utf8_do_not_become_gain(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-encoding-") as directory:
            for kind in ("encoding", "utf8", "odd_utf16"):
                with self.subTest(kind=kind):
                    path = Path(directory) / f"{kind}.mp3"
                    shutil.copyfile(FIXTURES / "id3v24.mp3", path)
                    write_tags(path, VALUES, encoding=1 if kind == "odd_utf16" else 3,
                               version=3 if kind == "odd_utf16" else 4)
                    raw = bytearray(path.read_bytes())
                    key = "REPLAYGAIN_TRACK_GAIN"
                    if kind == "odd_utf16":
                        at = raw.index(key.encode("utf-16le"))
                        frame = at - 13
                        self.assertEqual(b"TXXX", raw[frame:frame + 4])
                        size = int.from_bytes(raw[frame + 4:frame + 8], "big")
                        raw[frame + 4:frame + 8] = (size - 1).to_bytes(4, "big")
                    else:
                        at = raw.index(key.encode() + b"\x00")
                        self.assertEqual(3, raw[at - 1])
                        if kind == "encoding":
                            raw[at - 1] = 4
                        else:
                            value = at + len(key) + 1
                            self.assertEqual(b"+6", raw[value:value + 2])
                            raw[value:value + 2] = b"\xc0\xb6"
                    path.write_bytes(raw)
                    self.assertEqual(0, read_gains(path)[0])

    def test_embedded_null_asf_values_do_not_join_into_a_number(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-asf-") as directory:
            path = Path(directory) / "track.wma"
            shutil.copyfile(FIXTURES / "tagged.wma", path)
            write_tags(path, {"REPLAYGAIN_TRACK_GAIN": "1\x002 dB"})
            self.assertEqual(0, read_gains(path)[0])

    def test_unknown_namespaces_and_invalid_values_do_not_become_gain(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-invalid-") as directory:
            path = Path(directory) / "track.m4a"
            shutil.copyfile(FIXTURES / "aac.m4a", path)
            write_tags(path, VALUES, namespace="org.unrelated")
            self.assertEqual((0, 0, 0, 0), read_gains(path))
            write_tags(path, {"REPLAYGAIN_TRACK_GAIN": "6.0 dB junk",
                              "REPLAYGAIN_ALBUM_GAIN": "1" * 2000})
            self.assertEqual((0, 0, 0, 0), read_gains(path))
            self.assertEqual((0, 0, 0, 0), read_gains(FIXTURES / "untagged.flac"))


def main():
    global READER, FIXTURES
    parser = argparse.ArgumentParser()
    parser.add_argument("--reader", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    arguments, rest = parser.parse_known_args()
    READER, FIXTURES = arguments.reader, arguments.fixtures
    unittest.main(argv=[__file__, *rest])


if __name__ == "__main__":
    main()
