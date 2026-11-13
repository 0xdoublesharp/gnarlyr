import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tag_skim


class BoundedMetadataTest(unittest.TestCase):
    def test_corrupt_flac_padding_never_extends_the_stream_beyond_the_actual_file(self):
        data = b"fLaC" + bytes([0x81, 0, 0, 100]) + b"abc"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "truncated.flac")
            path.write_bytes(data)
            with tag_skim.open_skimming(path) as stream:
                self.assertEqual(len(data), len(stream.read(256)))
                self.assertEqual(b"", stream.read(256))

    def test_corrupt_id3_and_flac_picture_ranges_are_bounded_for_every_read_interface(self):
        picture = bytes(28) + (250).to_bytes(4, "big") + b"abc"
        flac_picture = b"fLaC" + bytes([0x86]) + (300).to_bytes(3, "big") + picture
        id3_frame = b"APIC" + bytes([0, 0, 0, 100, 0, 0]) + b"abc"
        inputs = [flac_picture]
        for version in (3, 4):
            inputs.append(b"ID3" + bytes([version, 0, 0, 0, 0, 0, 110]) + id3_frame)
        for raw in inputs:
            for method in ("read", "readall", "readinto"):
                with self.subTest(size=len(raw), method=method), tempfile.TemporaryDirectory() as directory:
                    path = Path(directory, "broken.audio")
                    path.write_bytes(raw)
                    with tag_skim.open_skimming(path) as stream:
                        if method == "readinto":
                            count = stream.readinto(bytearray(512))
                        else:
                            count = len(getattr(stream, method)())
                        self.assertEqual(len(raw), count)
                        self.assertEqual(len(raw), stream.tell())
                        self.assertEqual(b"", stream.read(1))
                    self.assertEqual(raw, path.read_bytes())

    def test_declared_padding_lengths_never_control_the_logical_file_length(self):
        for length in (0, 1, 3, 4, 100, 65535, 0xFFFFFF):
            raw = b"fLaC" + bytes([0x81]) + length.to_bytes(3, "big") + b"abc"
            with self.subTest(declared=length), tempfile.TemporaryDirectory() as directory:
                path = Path(directory, "broken.flac")
                path.write_bytes(raw)
                with tag_skim.open_skimming(path) as stream:
                    self.assertEqual(len(raw), len(stream.read(512)))


if __name__ == "__main__":
    unittest.main()
