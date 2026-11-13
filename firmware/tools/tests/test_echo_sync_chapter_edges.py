import io
import random
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import m4b_fixture as fixture
import mp4_chapters as chapters
from test_mp4_chapters import text_book


class ChapterBoundaryTest(unittest.TestCase):
    def test_underlying_short_reads_and_truncated_tables_are_rejected(self):
        reader = chapters.Reader(io.BytesIO(b"x"), 10)
        with self.assertRaisesRegex(chapters.InvalidChapters, "truncated"):
            reader.read(0, 2)
        table = fixture.table(b"stts", 2, [struct.pack(">II", 1, 1)])
        reader = chapters.Reader(io.BytesIO(table), len(table))
        with self.assertRaisesRegex(chapters.InvalidChapters, "truncated"):
            reader.table(chapters.Atom(b"stts", 8, len(table)), 8)
        empty = fixture.table(b"stts", 0, [])
        reader = chapters.Reader(io.BytesIO(empty), len(empty))
        self.assertEqual([], list(chapters.time_values(reader, chapters.Atom(b"stts", 8, len(empty)), 0)))

    def test_bad_referenced_track_headers_do_not_publish_chapters(self):
        raw = fixture.box(b"moov", fixture.audio_track(1000, chapter_track=2), fixture.box(b"trak", fixture.box(b"tkhd", b"")))
        reader = chapters.Reader(io.BytesIO(raw), len(raw))
        with self.assertRaisesRegex(chapters.InvalidChapters, "missing"):
            chapters.chapter_track(reader, chapters.Atom(b"moov", 8, len(raw)))

    def test_every_sample_table_and_media_clock_boundary_keeps_whole_book(self):
        original = text_book(fixture.text_samples(["One", "Two", "Three"]))
        cases = [(b"stsz", 12, 4), (b"stsc", 8, 0), (b"stsc", 12, 2),
                 (b"stsc", 16, 0), (b"stco", 8, 0), (b"stsc", 16, 1),
                 (b"mdhd", 16, 0)]
        for atom, offset, value in cases:
            raw = bytearray(original)
            location = raw.rindex(atom)
            struct.pack_into(">I", raw, location + offset, value)
            with self.subTest(atom=atom, offset=offset, value=value):
                self.assertEqual([], chapters.read_chapters(io.BytesIO(raw), len(raw), 3000))
        raw = bytearray(original)
        raw[raw.rindex(b"mdhd") + 4] = 2
        self.assertEqual([], chapters.read_chapters(io.BytesIO(raw), len(raw), 3000))

    def test_zero_sample_tables_have_no_chunks_and_no_timing(self):
        sizes = fixture.full(b"stsz", struct.pack(">II", 0, 0))
        mapping = fixture.table(b"stsc", 1, [struct.pack(">III", 1, 1, 1)])
        chunks = fixture.table(b"stco", 0, [])
        raw = sizes + mapping + chunks
        reader = chapters.Reader(io.BytesIO(raw), len(raw))
        self.assertEqual([], list(chapters.sample_places(reader, chapters.Atom(b"stbl", 0, len(raw)), 0)))

    def test_deterministic_corruption_and_truncation_are_bounded_and_preserve_input(self):
        originals = [text_book(fixture.text_samples(["One", "Two", "Three"])),
                     fixture.with_chpl("Book", 300000, [(0, "One"), (100000, "Two")])]
        randomizer = random.Random(72019)
        for original in originals:
            samples = [original[:length] for length in range(len(original))]
            for _ in range(500):
                raw = bytearray(original)
                for _ in range(randomizer.randint(1, 4)):
                    raw[randomizer.randrange(len(raw))] = randomizer.randrange(256)
                samples.append(bytes(raw))
            for raw in samples:
                handle = io.BytesIO(raw)
                found = chapters.read_chapters(handle, len(raw), 300000)
                self.assertLessEqual(len(found), 255)
                self.assertTrue(all(0 <= c.start_ms < 300000 and len(c.title.encode("utf-8")) <= 63 for c in found))
                self.assertTrue(all(a.start_ms < b.start_ms for a, b in zip(found, found[1:])))
                self.assertEqual(raw, handle.getvalue())


if __name__ == "__main__":
    unittest.main()
