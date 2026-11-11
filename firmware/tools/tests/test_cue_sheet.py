import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import cue_sheet
import library_index
from test_library_index import write_silent_flac

FILE_HEADER = 'FILE "a.flac" WAVE\n'


def two_tracks(first="00:00:00", second="01:00:00"):
    return (f"  TRACK 01 AUDIO\n    TITLE \"One\"\n    INDEX 01 {first}\n"
            f"  TRACK 02 AUDIO\n    TITLE \"Two\"\n    PERFORMER \"Guest\"\n    INDEX 01 {second}\n")


class ParseCueTest(unittest.TestCase):
    def test_a_track_takes_its_fields(self):
        sheet = cue_sheet.parse_cue(FILE_HEADER + two_tracks("00:00:00", "03:20:00"))
        self.assertEqual("a.flac", sheet.file)
        self.assertEqual([("One", "", 0), ("Two", "Guest", 200000)],
                         [(t.title, t.performer, t.start_ms) for t in sheet.tracks])

    def test_lines_before_the_first_track_describe_the_album(self):
        sheet = cue_sheet.parse_cue("REM GENRE Alternative Rock\nREM DATE 1990\nREM COMMENT x\n"
                                    'PERFORMER "Group"\nTITLE "Record"\n' + FILE_HEADER + two_tracks())
        self.assertEqual(("Alternative Rock", "1990", "Group", "Record"),
                         (sheet.genre, sheet.date, sheet.performer, sheet.title))
        self.assertEqual("", sheet.tracks[0].performer)

    def test_index_times_count_seventy_five_frames_per_second(self):
        self.assertEqual(13, cue_sheet.index_start("01 00:00:01"))
        self.assertEqual(754746, cue_sheet.index_start("01 12:34:56"))
        self.assertEqual(-1, cue_sheet.index_start("00 00:00:01"))
        self.assertEqual(-1, cue_sheet.index_start("01 00:00"))

    def test_index_fields_stay_within_cue_limits_and_end_at_the_line(self):
        self.assertEqual(-1, cue_sheet.index_start("01 00:60:00"))
        self.assertEqual(-1, cue_sheet.index_start("01 00:00:75"))
        self.assertEqual(-1, cue_sheet.index_start("01 00:00:00 trailing"))

    def test_keywords_ignore_case(self):
        sheet = cue_sheet.parse_cue('file "a.wav" wave\nrem date 2001\nTrAcK 03 audio\n'
                                    "title Plain Title\nPERFORMER Someone Else\nindex 01 00:01:00\n")
        track = sheet.tracks[0]
        self.assertEqual((3, "Plain Title", "Someone Else", "2001"),
                         (track.number, track.title, track.performer, sheet.date))

    def test_quoting_follows_the_cue_rules(self):
        sheet = cue_sheet.parse_cue('FILE "my file.wav" WAVE\nTRACK 01 AUDIO\nTITLE "A \\" B"\n'
                                    'PERFORMER "X" tail\nINDEX 01 00:00:00\n')
        self.assertEqual(("my file.wav", "A \\", "X"),
                         (sheet.file, sheet.tracks[0].title, sheet.tracks[0].performer))
        sheet = cue_sheet.parse_cue('FILE unquoted.wav WAVE\nTRACK 01 AUDIO\nTITLE "open\n'
                                    "INDEX 01 00:00:00\n")
        self.assertEqual(("unquoted.wav", "open"), (sheet.file, sheet.tracks[0].title))

    def test_a_track_without_index_01_is_dropped(self):
        sheet = cue_sheet.parse_cue(FILE_HEADER + "TRACK 01 AUDIO\nINDEX 01 00:00:00\n"
                                    "TRACK 02 AUDIO\nINDEX 00 00:10:00\nTRACK 03 AUDIO\n"
                                    "INDEX 00 00:20:00\nINDEX 01 00:30:00\n")
        self.assertEqual([(1, 0), (3, 30000)], [(t.number, t.start_ms) for t in sheet.tracks])

    def test_track_numbers_must_be_between_one_and_ninety_nine(self):
        sheet = cue_sheet.parse_cue(FILE_HEADER +
                                    "TRACK AUDIO\nINDEX 01 00:00:00\n"
                                    "TRACK 00 AUDIO\nTITLE Not Album\nPERFORMER Not Artist\n"
                                    "INDEX 01 00:01:00\n"
                                    "TRACK 100 AUDIO\nINDEX 01 00:02:00\n"
                                    "TRACK 01AUDIO\nINDEX 01 00:03:00\n"
                                    "TRACK 09 AUDIO\nINDEX 01 00:04:00\n")
        self.assertEqual([(9, 4000)], [(track.number, track.start_ms) for track in sheet.tracks])
        self.assertEqual(("", ""), (sheet.title, sheet.performer))

    def test_equal_or_descending_starts_are_dropped(self):
        sheet = cue_sheet.parse_cue(FILE_HEADER +
                                    "TRACK 01 AUDIO\nINDEX 01 00:10:00\n"
                                    "TRACK 02 AUDIO\nINDEX 01 00:05:00\n"
                                    "TRACK 03 AUDIO\nINDEX 01 00:10:00\n"
                                    "TRACK 04 AUDIO\nINDEX 01 00:15:00\n")
        self.assertEqual([(1, 10000), (4, 15000)],
                         [(track.number, track.start_ms) for track in sheet.tracks])

    def test_a_sheet_needs_one_file_and_a_track(self):
        self.assertIsNotNone(cue_sheet.parse_cue(FILE_HEADER + two_tracks()))
        self.assertIsNone(cue_sheet.parse_cue(FILE_HEADER + two_tracks() + FILE_HEADER + two_tracks()))
        self.assertIsNone(cue_sheet.parse_cue(FILE_HEADER + "TRACK 01 AUDIO\nINDEX 00 00:00:00\n"))
        self.assertIsNone(cue_sheet.parse_cue(two_tracks()))
        self.assertIsNone(cue_sheet.parse_cue(""))


class CueAudioTest(unittest.TestCase):
    def audio(self, file, cue, names):
        sheet = cue_sheet.parse_cue(f'FILE "{file}" WAVE\n' + two_tracks())
        found = library_index.cue_audio(sheet, Path(cue), [Path("d") / name for name in names])
        return found.name if found else None

    def test_the_file_named_in_the_sheet_wins_ignoring_case(self):
        self.assertEqual("Disc.FLAC", self.audio("disc.flac", "x.cue", ["a.flac", "Disc.FLAC"]))

    def test_a_folder_prefix_in_the_sheet_is_ignored(self):
        self.assertEqual("disc.flac", self.audio("C:\\Rips\\disc.flac", "x.cue", ["disc.flac"]))

    def test_the_same_named_audio_file_is_the_fallback(self):
        self.assertEqual("album.flac", self.audio("Album.wav", "Album.cue", ["other.mp3", "album.flac"]))

    def test_formats_other_than_flac_wav_and_mp3_are_not_candidates(self):
        self.assertEqual("album.wav", self.audio("album.ape", "album.cue", ["album.wav"]))

    def test_nothing_matching_gives_no_audio(self):
        self.assertIsNone(self.audio("gone.flac", "x.cue", ["a.flac"]))


class CueDecodingTest(unittest.TestCase):
    def test_utf8_with_a_byte_order_mark_and_crlf(self):
        data = ('\ufeffTITLE "Caf\xe9"\r\nFILE "a.flac" WAVE\r\n' + two_tracks()).encode("utf-8")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.cue"
            path.write_bytes(data)
            self.assertEqual("Caf\xe9", library_index.read_cue(path).title)

    def test_text_that_is_not_utf8_reads_as_windows_1252(self):
        data = ('TITLE "Caf\xe9 \u20ac"\nFILE "a.flac" WAVE\n' + two_tracks()).encode("cp1252")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.cue"
            path.write_bytes(data)
            self.assertEqual("Caf\xe9 \u20ac", library_index.read_cue(path).title)


class ScanCueTest(unittest.TestCase):
    def scan(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                if isinstance(content, int):
                    write_silent_flac(path, content)
                else:
                    path.write_bytes(content.encode("utf-8"))
            tracks, _, failures = library_index.scan(root)
            return tracks, failures

    def test_a_sheet_makes_a_track_per_cue_track_and_hides_its_audio(self):
        sheet = ('PERFORMER "Band"\nTITLE "Record"\nREM GENRE Pop\nREM DATE 1999\n' + FILE_HEADER +
                 two_tracks("00:00:00", "00:30:00"))
        tracks, failures = self.scan({"Band/Record/a.flac": 90, "Band/Record/a.cue": sheet})
        self.assertEqual([], failures)
        self.assertEqual({"Band/Record/a.flac"}, {t.path for t in tracks})
        self.assertEqual([(1, "One", 0, 30000, 30000), (2, "Two", 30000, 0, 60000)],
                         [(t.number, t.title, t.start_ms, t.end_ms, t.duration_ms) for t in tracks])
        first, second = tracks
        self.assertEqual(("Record", "Band", "Band", "Pop", 1999),
                         (first.album, first.artist, first.album_artist, first.genre, first.year))
        self.assertEqual(("Guest", "Band"), (second.artist, second.album_artist))

    def test_the_cue_beats_the_file_tags_and_missing_fields_fall_back(self):
        tracks, _ = self.scan({"X/Y/a.flac": 90, "X/Y/a.cue": FILE_HEADER + two_tracks()})
        self.assertEqual(["Y", "Y"], [t.album for t in tracks])
        self.assertEqual(["Unknown Artist", "Guest"], [t.artist for t in tracks])
        self.assertTrue(all(t.album_artist_guessed for t in tracks))

    def test_an_untitled_cue_track_is_named_after_its_number(self):
        sheet = FILE_HEADER + "TRACK 07 AUDIO\nINDEX 01 00:00:00\n"
        tracks, _ = self.scan({"a.flac": 5, "a.cue": sheet})
        self.assertEqual(["Track 7"], [t.title for t in tracks])
        self.assertEqual([(0, 0)], [(t.start_ms, t.end_ms) for t in tracks])

    def test_a_wrongly_named_file_falls_back_to_the_same_named_audio(self):
        sheet = 'FILE "Something Else.wav" WAVE\n' + two_tracks()
        tracks, _ = self.scan({"Disc.flac": 90, "Disc.cue": sheet, "Other.flac": 3})
        self.assertEqual(["Disc.flac", "Disc.flac", "Other.flac"], [Path(t.path).name for t in tracks])

    def test_a_sheet_that_cannot_be_used_leaves_the_audio_alone(self):
        split = FILE_HEADER + two_tracks() + 'FILE "b.flac" WAVE\n' + two_tracks()
        tracks, _ = self.scan({"a.flac": 5, "split.cue": split, "gone.cue": 'FILE "x.flac" WAVE\n' + two_tracks()})
        self.assertEqual([("a.flac", 0, 0)], [(Path(t.path).name, t.start_ms, t.end_ms) for t in tracks])

    def test_the_database_records_start_and_end(self):
        tracks, _ = self.scan({"a.flac": 90, "a.cue": FILE_HEADER + two_tracks("00:00:00", "00:30:00")})
        data = library_index.encode_library(library_index.build_library(tracks))
        offset = library_index.SECTION_ENTRY.unpack_from(data, 8)[0]
        records = [library_index.TRACK_RECORD.unpack_from(data, offset + n * 32) for n in range(2)]
        self.assertEqual([(0, 30000), (30000, 0)], [(r[10], r[11]) for r in records])


if __name__ == "__main__":
    unittest.main()
