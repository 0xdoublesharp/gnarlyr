import sys
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import test_library

OPEN = "Attribution-ShareAlike 4.0"
CLOSED = "Attribution-NonCommercial-NoDerivatives 4.0"


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


if __name__ == "__main__":
    unittest.main()
