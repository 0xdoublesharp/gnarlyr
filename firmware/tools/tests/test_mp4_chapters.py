import io
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import m4b_fixture as fixture
from mp4_chapters import Chapter, read_chapters


def text_book(samples, fixed=False, wide=False, version=0, offset_shift=0, deltas=None):
    def build(base):
        count = len(samples)
        header = fixture.mdhd(1000, count * 1000) if not version else fixture.full(
            b"mdhd", struct.pack(">QQIQHH", 0, 0, 1000, count * 1000, 0, 0), version=1)
        times = fixture.table(b"stts", count, [struct.pack(">II", 1, delta)
                              for delta in (deltas or [1000] * count)])
        sizes = fixture.full(b"stsz", struct.pack(">II", len(samples[0]) if fixed else 0, count),
                             b"" if fixed else b"".join(struct.pack(">I", len(s)) for s in samples))
        mapping = fixture.table(b"stsc", 1, [struct.pack(">III", 1, count, 1)])
        chunks = fixture.table(b"co64" if wide else b"stco", 1,
                               [struct.pack(">Q" if wide else ">I", base + offset_shift)])
        tables = fixture.box(b"stbl", times, sizes, mapping, chunks)
        media = fixture.box(b"mdia", header, fixture.hdlr(b"text"), fixture.box(b"minf", tables))
        track = fixture.box(b"trak", fixture.tkhd(2), media)
        return [fixture.audio_track(count * 1000, chapter_track=2), track]
    return fixture.assemble(build, b"".join(samples))


def chapters(raw, duration=300000):
    return read_chapters(io.BytesIO(raw), len(raw), duration)


class CountingFile(io.BytesIO):
    def __init__(self, raw):
        super().__init__(raw)
        self.bytes_read = 0
        self.largest_read = 0

    def read(self, length=-1):
        data = super().read(length)
        self.bytes_read += len(data)
        self.largest_read = max(self.largest_read, len(data))
        return data


class ChapterParserTest(unittest.TestCase):
    def test_first_chapter_starts_at_zero_and_single_entries_remain_whole_books(self):
        raw = fixture.with_chpl("Book", 300000, [(5000, "A"), (100000, "B")])
        self.assertEqual([Chapter(0, "A"), Chapter(100000, "B")], chapters(raw))
        self.assertEqual([], chapters(fixture.with_chpl("Book", 300000, [(5000, "A")])))

    def test_truncated_atom_and_chapter_rows_do_not_publish_partial_lists(self):
        raw = fixture.with_chpl("Book", 300000, [(0, "A"), (100000, "B")])
        for missing in [1, 5, 12, 30]:
            with self.subTest(missing=missing):
                self.assertEqual([], chapters(raw[:-missing]))
        raw = fixture.with_chpl("Book", 300000, [(0, "A"), (100000, "B")], declared=3)
        self.assertEqual([], chapters(raw))

    def test_wide_and_to_end_container_sizes_are_supported(self):
        raw = fixture.with_chpl("Book", 300000, [(0, "A"), (100000, "B")])
        moov = raw.index(b"moov") - 4
        wide = raw[:moov] + struct.pack(">I4sQ", 1, b"moov", len(raw) - moov + 8) + raw[moov + 8:]
        to_end = raw[:moov] + bytes(4) + raw[moov + 4:]
        self.assertEqual(chapters(raw), chapters(wide))
        self.assertEqual(chapters(raw), chapters(to_end))

    def test_broken_atom_sizes_are_ignored_without_looping_or_overreading(self):
        for length in [0, 1, 4, 7, 0xFFFFFFFF]:
            with self.subTest(length=length):
                self.assertEqual([], chapters(struct.pack(">I4s", length, b"moov")))

    def test_chpl_times_beyond_the_player_clock_are_rejected(self):
        raw = fixture.with_chpl("Book", 300000, [(0, "A"), (0x100000000, "B")])
        self.assertEqual([], chapters(raw))

    def test_text_tracks_support_fixed_samples_wide_offsets_and_v1_media_clocks(self):
        samples = fixture.text_samples(["One", "Two", "End"])
        for fixed in [False, True]:
            for wide in [False, True]:
                for version in [0, 1]:
                    with self.subTest(fixed=fixed, wide=wide, version=version):
                        raw = text_book(samples, fixed=fixed, wide=wide, version=version)
                        self.assertEqual([Chapter(0, "One"), Chapter(1000, "Two"),
                                          Chapter(2000, "End")], chapters(raw, 3000))

    def test_out_of_bounds_samples_and_incomplete_timing_keep_the_whole_book(self):
        samples = fixture.text_samples(["One", "Two", "End"])
        raw = text_book(samples, offset_shift=100000)
        self.assertEqual([], chapters(raw))
        raw = text_book(samples)
        timing = raw.index(b"stts")
        raw = raw[:timing + 8] + struct.pack(">I", 2) + raw[timing + 12:]
        self.assertEqual([], chapters(raw))
        wide = text_book(samples, wide=True, offset_shift=0x100000000)
        self.assertEqual([], chapters(wide))

    def test_empty_utf16_and_long_utf8_titles_match_player_fallbacks(self):
        samples = [struct.pack(">H", 4) + b"\xfe\xff\0A", b"\0\0",
                   fixture.text_samples(["é" * 35])[0]]
        raw = text_book(samples)
        self.assertEqual([Chapter(0, "Chapter 1"), Chapter(1000, "Chapter 2"),
                          Chapter(2000, "é" * 31)], chapters(raw, 3000))

    def test_rejected_chpl_falls_back_to_the_referenced_text_track(self):
        raw = text_book(fixture.text_samples(["One", "Two"]))
        moov = raw.index(b"moov") - 4
        extra = fixture.box(b"udta", fixture.chpl([(0, "A"), (1000, "B")], declared=5))
        size = int.from_bytes(raw[moov:moov + 4], "big")
        raw = raw[:moov] + struct.pack(">I", size + len(extra)) + raw[moov + 4:] + extra
        self.assertEqual([Chapter(0, "One"), Chapter(1000, "Two")], chapters(raw, 2000))

    def test_large_audio_payload_is_skipped_and_chapter_work_is_bounded(self):
        def build(_base):
            return [fixture.audio_track(300000), fixture.box(
                b"udta", fixture.chpl([(0, "A"), (100000, "B")]))]
        raw = fixture.assemble(build, bytes(1024 * 1024))
        handle = CountingFile(raw)
        self.assertEqual([Chapter(0, "A"), Chapter(100000, "B")],
                         read_chapters(handle, len(raw), 300000))
        self.assertLess(handle.bytes_read, 256)
        self.assertLessEqual(handle.largest_read, 9)

    def test_only_the_first_255_text_chapters_are_exposed(self):
        raw = text_book(fixture.text_samples(["Part"] * 300))
        found = chapters(raw, 300000)
        self.assertEqual(255, len(found))
        self.assertEqual(254000, found[-1].start_ms)


if __name__ == "__main__":
    unittest.main()
