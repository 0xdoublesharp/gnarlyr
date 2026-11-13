import contextlib
import io
import runpy
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import cover_export
import cue_sheet
import dsd_metadata
import library_index
import replay_gain
import tag_skim


class MetadataBoundaryTest(unittest.TestCase):
    def test_invalid_numeric_tags_are_missing_and_r128_rounds_signed_values(self):
        for value in (None, 1, "", "é", "1" * 65, "-0.01", "1e100", "1 dB", "256"):
            self.assertIsNone(replay_gain.parse_number(value, False), value)
        for value in (None, 1, "", "abc", "1234567", "+123456", "32768", "-32769", "32767", "-32768"):
            self.assertIsNone(replay_gain.parse_r128(value), value)
        self.assertEqual(500, replay_gain.parse_r128("0"))
        self.assertEqual(-1, replay_gain.parse_r128("-1283"))
        self.assertEqual(1, replay_gain.parse_r128("-1277"))
        self.assertEqual(0, replay_gain.amplitude(-9601))
        self.assertEqual(0, replay_gain.amplitude(9601))
        self.assertEqual(b"", replay_gain.encode_extension([(0, 0, 0, 0)], 0))

    def test_gain_tags_are_normalized_from_actual_mutagen_containers(self):
        from mutagen.apev2 import APEv2, APEBinaryValue
        from mutagen.asf import ASFTags, ASFUnicodeAttribute, ASFDWordAttribute
        from mutagen.id3 import ID3, TXXX
        from mutagen.mp4 import MP4Tags, MP4FreeForm
        from mutagen.oggopus import OggOpusVComment
        id3 = ID3()
        id3.add(TXXX(encoding=3, desc="REPLAYGAIN_TRACK_GAIN", text=["-1 dB", "ignored"]))
        mp4 = MP4Tags()
        mp4["----:com.apple.iTunes:replaygain_track_gain"] = [MP4FreeForm(b"-1 dB"), MP4FreeForm(b"ignored", dataformat=0)]
        mp4["©nam"] = ["ignored"]
        ape = APEv2()
        ape["replaygain_track_gain"] = "-1 dB"
        ape["replaygain_album_gain"] = ["1 dB", "2 dB"]
        ape["replaygain_track_peak"] = APEBinaryValue(b"binary")
        asf = ASFTags()
        asf["replaygain_track_gain"] = [ASFUnicodeAttribute("-1 dB"), ASFDWordAttribute(1)]
        for tags in (id3, mp4, ape, asf):
            self.assertEqual(["-1 dB"], replay_gain.tag_values(tags)["replaygain_track_gain"])
        opus = OggOpusVComment.__new__(OggOpusVComment)
        list.__init__(opus)
        opus["r128_track_gain"] = ["bad", "-1536"]
        opus["r128_album_gain"] = ["0"]
        self.assertEqual({"replaygain_track_gain": ["-1.00"], "replaygain_album_gain": ["5.00"]}, replay_gain.tag_values(opus))
        self.assertEqual({}, replay_gain.tag_values({"é": ["1"]}))

    def test_short_dsd_chunks_and_invalid_container_sizes_never_publish_partial_fields(self):
        def chunk(tag, data):
            return struct.pack(">4sQ", tag, len(data)) + data + bytes(len(data) & 1)
        def form(data):
            return b"FRM8" + struct.pack(">Q", len(data) + 4) + b"DSD " + data
        for raw in (b"", bytes(16), b"FRM8" + bytes(12), b"FRM8" + struct.pack(">Q", 999) + b"DSD "):
            self.assertEqual({}, dsd_metadata.diin_tags(io.BytesIO(raw)))
        self.assertEqual([], list(dsd_metadata.chunks(io.BytesIO(b"short"), 0, 12)))
        for count, end in ((1, 13), (20, 12)):
            self.assertEqual([], list(dsd_metadata.chunks(io.BytesIO(struct.pack(">4sQ", b"JUNK", count)), 0, end)))
        fields = chunk(b"NOPE", b"ignored") + chunk(b"DIAR", b"x") + chunk(b"DITI", struct.pack(">I", 100) + b"short")
        raw = form(chunk(b"JUNK", b"ignored") + chunk(b"DIIN", fields))
        self.assertEqual({}, dsd_metadata.diin_tags(io.BytesIO(raw)))
        raw = form(chunk(b"DIIN", chunk(b"DIAR", struct.pack(">I", 1) + b"A")))
        class ShortLength(io.BytesIO):
            def read(self, count=-1):
                return b"" if count == 4 else super().read(count)
        self.assertEqual({}, dsd_metadata.diin_tags(ShortLength(raw)))

    def test_skimming_stream_capabilities_close_and_open_failure_cleanup(self):
        self.assertEqual([], tag_skim.flac_picture_ranges(io.BytesIO(b"fLaC")))
        self.assertEqual([], tag_skim.id3_picture_ranges(io.BytesIO(b"ID3\3\0\0\0\0\0\0")))
        reader = tag_skim.SkimmingReader("fixture", io.BytesIO(b""), [], 10)
        self.assertTrue(reader.readable())
        self.assertTrue(reader.seekable())
        self.assertEqual(0, reader.readinto(bytearray(10)))
        reader.close()
        reader.close()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "track")
            path.write_bytes(b"source")
            handle = path.open("rb")
            with mock.patch.object(tag_skim.os, "fdopen", return_value=handle), mock.patch.object(tag_skim.os, "open", return_value=-1), mock.patch.object(tag_skim, "picture_ranges", side_effect=OSError("metadata I/O")):
                with self.assertRaises(OSError):
                    tag_skim.open_skimming(path)
            self.assertTrue(handle.closed)

    def test_cue_and_library_empty_fields_and_unreadable_inputs_are_bounded(self):
        sheet = cue_sheet.CueSheet()
        cue_sheet.apply_rem(sheet, "")
        self.assertIsNone(cue_sheet.parse_cue("\n  \nREM\n"))
        self.assertEqual("value", library_index.first_tag({"title": [" ", "value"]}, "title"))
        self.assertEqual({}, library_index.ape_values({"title": SimpleNamespace(kind=1)}))
        self.assertEqual({}, library_index.asf_values({}))
        self.assertIsNone(library_index.PathIndex([]).resolve_by_suffix(()))
        with self.assertRaises(ValueError):
            library_index.checked_index(65536)
        with tempfile.TemporaryDirectory() as directory:
            self.assertIsNone(library_index.read_cue(Path(directory, "missing.cue")))

    def test_mp3_ape_tail_tags_are_read_without_id3_and_source_bytes_are_preserved(self):
        from mutagen.apev2 import APEv2
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "song.mp3"
            tags = APEv2()
            tags["title"] = "APE title"
            tags["replaygain_track_gain"] = "-1 dB"
            tags.save(path)
            before = path.read_bytes()
            with mock.patch("mutagen.File", return_value=None):
                track = library_index.read_track(root, path)
            self.assertEqual("APE title", track.title)
            self.assertGreater(track.replay_gain[0], 0)
            self.assertEqual(before, path.read_bytes())

    def test_cover_metadata_and_decoder_rejections(self):
        from mutagen.apev2 import APEv2, APEBinaryValue
        tags = APEv2()
        tags["cover art (front)"] = APEBinaryValue(b"unterminated")
        tags["cover art ("] = APEBinaryValue(b"name\0bytes")
        tags["unrelated"] = "value"
        self.assertEqual([], cover_export.ape_pictures(tags))
        self.assertEqual(2, cover_export.skip_utf16_string(b"aa", 0))
        self.assertIsNone(cover_export.asf_picture(b"x"))
        self.assertIsNone(cover_export.asf_picture(struct.pack("<BI", 99, 2) + bytes(4)))
        self.assertEqual([], cover_export.asf_pictures({"WM/Picture": [SimpleNamespace(value=1), SimpleNamespace(value=b"short")]}))
        self.assertEqual([], cover_export.tag_pictures({}))
        with mock.patch("mutagen.File", return_value=None):
            self.assertEqual([], cover_export.embedded_pictures("fixture"))
        for raw in (b"\xff\xd8\xff\xff\xff\xda\0\2", b"\xff\xd8\xff\xda\0\2", b"\xff\xd8\xff\xe0\0\0", b"\xff\xd8"):
            self.assertFalse(cover_export.jpeg_frame_supported(raw))
        self.assertIsNone(cover_export.locate_art(Path("."), []))
        self.assertIsNone(cover_export.converted(SimpleNamespace(image=1), lambda image: 1 / 0))
        self.assertEqual(1, cover_export.unchanged(1))
        art = cover_export.Art(b"", Path("/nonexistent/art"), "cover.png")
        self.assertEqual(cover_export.UNKNOWN_STAMP, cover_export.stamp_for(art))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "cover.png"
            image.write_bytes(b"image")
            with mock.patch.object(Path, "read_bytes", side_effect=OSError("denied")):
                self.assertIsNone(cover_export.folder_art(root, ["song.flac"]))
            art = cover_export.Art(b"", image, "cover.png")
            self.assertEqual(cover_export.UNKNOWN_STAMP, cover_export.stamp_for(art, lambda _: (_ for _ in ()).throw(OSError("denied"))))
            source = cover_export.AlbumSource(1, "Album", "Artist", ["song.flac"])
            with mock.patch.object(cover_export, "locate_art", return_value=cover_export.Art(b"", image, "cover.png", image=1)):
                self.assertEqual({1: 1}, cover_export.collect_covers(root, [source, source]).records)


if __name__ == "__main__":
    unittest.main()
