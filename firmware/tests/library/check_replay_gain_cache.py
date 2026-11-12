import argparse
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import library_index
import replay_gain
from fnv import fnv1a
from check_replay_gain_tags import FORMATS, VALUES, write_tags

BUILDER = None
SCANNER = None
FIXTURES = None


class ReplayGainCacheTest(unittest.TestCase):
    def test_device_extension_matches_desktop_and_both_databases_read_identically(self):
        tracks = []
        for index in (2, 1):
            tracks.append(library_index.TrackInfo(
                path="a.flac" if index == 1 else "b.flac", title="A" if index == 1 else "B",
                artist="Artist", album_artist="Artist", album="Album", genre="Genre",
                year=0, disc=1, number=index, duration_ms=60000,
                replay_gain=(100000 + index, 80000 + index, 50000 + index, 40000 + index)))
        expected = library_index.encode_library(library_index.build_library(tracks))
        with tempfile.TemporaryDirectory(prefix="replaygain-cache-") as directory:
            device = Path(directory) / "device.db"
            desktop = Path(directory) / "desktop.db"
            subprocess.run([str(BUILDER), "--write", str(device)], check=True)
            data = device.read_bytes()
            sections = [struct.unpack_from("<IIHH", data, 8 + 12 * index) for index in range(10)]
            end = max(offset + count * size for offset, count, size, _ in sections)
            fingerprint = fnv1a(data[:128])
            for offset, count, size, _ in sections:
                fingerprint = fnv1a(data[offset:offset + count * size], fingerprint)
            ordered = [track.replay_gain for track in reversed(tracks)]
            self.assertEqual(replay_gain.encode_extension(ordered, fingerprint), data[end:])
            desktop.write_bytes(expected)
            for path in (device, desktop):
                output = subprocess.check_output([str(BUILDER), "--read", str(path)], text=True)
                gains = [tuple(map(int, line.split())) for line in output.splitlines()]
                self.assertEqual(ordered, gains)



    def test_device_and_desktop_scans_keep_real_tags_cue_parts_and_missing_values(self):
        with tempfile.TemporaryDirectory(prefix="replaygain-scanners-") as directory:
            root = Path(directory)
            music = root / "music"
            music.mkdir()
            for name in FORMATS:
                path = music / name
                shutil.copyfile(FIXTURES / name, path)
                write_tags(path, VALUES)
            shutil.copyfile(FIXTURES / "untagged.flac", music / "untagged.flac")
            (music / "album.cue").write_text(
                'TITLE "Cue album"\nFILE "tagged.flac" WAVE\n'
                'TRACK 01 AUDIO\nTITLE "First"\nINDEX 01 00:00:00\n'
                'TRACK 02 AUDIO\nTITLE "Second"\nINDEX 01 00:00:01\n')
            tracks, playlists, failures = library_index.scan(music)
            self.assertEqual([], failures)
            self.assertGreater(sum(bool(track.replay_gain[0]) for track in tracks), len(FORMATS))
            expected = root / "library.db"
            expected.write_bytes(library_index.encode_library(
                library_index.build_library(tracks, playlists)))
            subprocess.run([str(SCANNER), "--compare", str(music), str(expected)], check=True)


def main():
    global BUILDER, SCANNER, FIXTURES
    parser = argparse.ArgumentParser()
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--scanner", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    arguments, rest = parser.parse_known_args()
    BUILDER, SCANNER, FIXTURES = arguments.builder, arguments.scanner, arguments.fixtures
    unittest.main(argv=[__file__, *rest])


if __name__ == "__main__":
    main()
