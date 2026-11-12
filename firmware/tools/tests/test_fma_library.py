import csv
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import test_library

OPEN = "Attribution-ShareAlike 4.0"
CLOSED = "Attribution-NonCommercial-NoDerivatives 4.0"
GOOD_MP3 = (bytes.fromhex("fffb9064") + bytes(413)) * 20
BAD_MP3 = b"not audio" * 200


def catalog(albums, per_album, license_text=OPEN):
    tracks = []
    for album in range(albums):
        for number in range(1, per_album + 1):
            tracks.append(test_library.Track(album * 100 + number, "Song %d" % number, number,
                                             "Artist %d" % album, str(album), "Album %d" % album,
                                             "Rock", "2010", license_text))
    return tracks


class PlanTest(unittest.TestCase):
    def test_whole_albums_are_taken_until_the_count_is_reached(self):
        albums = test_library.plan_albums(catalog(50, 4), 30, 1)
        self.assertEqual(8, len(albums))
        self.assertEqual(32, sum(len(album.tracks) for album in albums))

    def test_the_plan_is_the_same_for_the_same_seed(self):
        first = test_library.plan_albums(catalog(80, 3), 120, 7)
        second = test_library.plan_albums(catalog(80, 3), 120, 7)
        self.assertEqual([(album.album_id, album.format, album.art, album.folder_name)
                          for album in first],
                         [(album.album_id, album.format, album.art, album.folder_name)
                          for album in second])

    def test_cue_and_book_albums_need_three_tracks_and_a_licence_allowing_derivatives(self):
        albums = test_library.plan_albums(catalog(40, 3), 120, 2)
        kinds = [album.kind for album in albums]
        self.assertEqual(2, kinds.count("cue"))
        self.assertEqual(2, kinds.count("book"))
        closed = test_library.plan_albums(catalog(40, 3, CLOSED), 120, 2)
        self.assertEqual({"plain"}, {album.kind for album in closed})

    def test_names_are_safe_on_fat_and_exfat(self):
        self.assertEqual("AC_DC_ Live", test_library.safe_name('AC/DC: Live', "x"))
        self.assertEqual("x", test_library.safe_name(" ... ", "x"))
        self.assertEqual(test_library.NAME_LIMIT, len(test_library.safe_name("a" * 200, "x")))

    def test_cue_indexes_follow_the_track_lengths_in_frames(self):
        album = test_library.plan_albums(catalog(1, 3), 3, 1)[0]
        text = test_library.cue_text(album, "Album 0.flac", [30.0, 30.5, 29.0])
        self.assertIn("INDEX 01 00:00:00", text)
        self.assertIn("INDEX 01 00:30:00", text)
        self.assertIn("INDEX 01 01:00:38", text)


class PlanMoreTest(unittest.TestCase):
    def test_more_albums_continue_the_same_order_after_the_plan(self):
        tracks = catalog(50, 4)
        planned = test_library.plan_albums(tracks, 30, 1)
        longer = test_library.plan_albums(tracks, 50, 1)
        more = test_library.more_albums(tracks, 1, planned, 9)
        self.assertEqual([album.album_id for album in longer[len(planned):len(planned) + 3]],
                         [album.album_id for album in more])
        self.assertEqual({"plain"}, {album.kind for album in more})


class WriteTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "build")
        self.root = Path(self.directory.name)
        self.archive = self.root / "fma_small.zip"
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr(test_library.source_member(1), GOOD_MP3)
            archive.writestr(test_library.source_member(2), BAD_MP3)
            archive.writestr(test_library.source_member(3), GOOD_MP3)
        self.out = self.root / "library"
        self.out.mkdir()

    def tearDown(self):
        self.directory.cleanup()

    def album(self, kind="plain"):
        album = test_library.plan_albums(catalog(1, 3), 3, 1)[0]
        for number, track in enumerate(album.tracks, 1):
            track.track_id = number
        album.kind, album.format, album.art, album.embedded = kind, "mp3", "none", False
        return album

    def test_an_unreadable_source_is_skipped_and_leaves_no_file(self):
        album = test_library.write_album(self.archive, self.out, self.album())
        self.assertEqual([1, 3], [track.track_id for track in album.written])
        self.assertEqual([2], [track_id for track_id, _ in album.skipped])
        names = sorted(path.name for path in self.out.rglob("*") if path.is_file())
        self.assertEqual(["01 Song 1.mp3", "03 Song 3.mp3"], names)

    def test_a_joined_album_with_an_unreadable_part_is_skipped_whole(self):
        album = test_library.write_album(self.archive, self.out, self.album("cue"))
        self.assertEqual([], album.written)
        self.assertEqual([2], [track_id for track_id, _ in album.skipped])
        self.assertEqual([], [path for path in self.out.rglob("*") if path.is_file()])

    def test_a_rerun_keeps_finished_files_and_rewrites_partial_ones(self):
        test_library.write_album(self.archive, self.out, self.album())
        folder = next(self.out.rglob("01 Song 1.mp3")).parent
        finished, partial = folder / "01 Song 1.mp3", folder / "03 Song 3.mp3"
        finished.write_bytes(finished.read_bytes() + b"kept")
        partial.write_bytes(GOOD_MP3[:500])
        (folder / ".source-leftover.mp3").write_bytes(b"x")
        test_library.clean_leftovers(self.out)
        album = test_library.write_album(self.archive, self.out, self.album())
        self.assertTrue(finished.read_bytes().endswith(b"kept"))
        self.assertTrue(test_library.finished(partial))
        self.assertEqual([1, 3], [track.track_id for track in album.written])
        self.assertFalse((folder / ".source-leftover.mp3").exists())


COLUMNS = [("set", "subset"), ("track", "number"), ("track", "title"), ("artist", "name"),
           ("album", "id"), ("album", "title"), ("track", "genre_top"), ("album", "date_released"),
           ("track", "license")]


def metadata_zip(path, tracks):
    text = io.StringIO()
    rows = csv.writer(text)
    rows.writerow([""] + [group for group, _ in COLUMNS])
    rows.writerow([""] + [name for _, name in COLUMNS])
    rows.writerow(["track_id"] + [""] * len(COLUMNS))
    for track in tracks:
        rows.writerow([track.track_id, "small", track.number, track.title, track.artist, track.album_id,
                       track.album, track.genre, track.year, track.license])
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr(test_library.METADATA_MEMBER, text.getvalue())


class BuildTest(unittest.TestCase):
    def test_skipped_tracks_are_made_up_from_the_next_albums_and_listed(self):
        tracks = catalog(6, 2)
        first_album = test_library.album_order(tracks, 1)[0][1]
        bad = first_album[0].track_id
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "build") as name:
            fma = Path(name) / "fma"
            fma.mkdir()
            metadata_zip(fma / "fma_metadata.zip", tracks)
            with zipfile.ZipFile(fma / "fma_small.zip", "w") as archive:
                for track in tracks:
                    archive.writestr(test_library.source_member(track.track_id),
                                     BAD_MP3 if track.track_id == bad else GOOD_MP3)
            original = test_library.decide
            test_library.decide = lambda album, generator: setattr(album, "art", "none") or setattr(
                album, "embedded", False)
            try:
                data = test_library.build(fma, Path(name) / "out", 4, 1, 1, log=lambda text: None)
            finally:
                test_library.decide = original
            self.assertEqual(5, data["summary"]["tracks"])
            self.assertEqual([bad], [item["fma_track_id"] for item in data["skipped"]])
            self.assertEqual(5, len(list((Path(name) / "out").rglob("*.mp3"))))


if __name__ == "__main__":
    unittest.main()
