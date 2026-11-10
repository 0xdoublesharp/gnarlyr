import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

import library_index


def main():
    parser = argparse.ArgumentParser(description="Compare audiobook recognition across indexers")
    parser.add_argument("--scanner", type=Path, required=True)
    arguments = parser.parse_args()
    fixtures = Path(__file__).resolve().parents[1] / "tests/fixtures"
    with tempfile.TemporaryDirectory(prefix="audiobook-index-") as folder:
        root = Path(folder) / "music"
        files = {
            "At Root.M4B": "m4b_real/Audiobooks/Real Book.m4b",
            "Downloads/Writer/Chapters.m4b": "m4b_music/Audiobooks/Chpl Book.m4b",
            "Downloads/Writer/Whole.m4b": "m4b_music/Audiobooks/Plain Book.m4b",
            "Audiobook/Named/Whole.m4b": "m4b_music/Audiobooks/Plain Book.m4b",
            "Audiobooks/Named/Chapters.m4b": "m4b_music/Audiobooks/Track Book.m4b",
            "Podcasts/Show/Episode.m4b": "m4b_music/Audiobooks/Plain Book.m4b",
            "Music/Album/Song.m4a": "m4a_music/Aac Artist/Aac Album/02 Tail.m4a",
        }
        for destination, source in files.items():
            path = root / destination
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(fixtures / source, path)
        tracks, playlists, failures = library_index.scan(root)
        if failures:
            raise RuntimeError(failures)
        model = library_index.build_library(tracks, playlists)
        if model.music_tracks != 1:
            raise RuntimeError(f"expected one music track, got {model.music_tracks}")
        expected = Path(folder) / "expected.db"
        expected.write_bytes(library_index.encode_library(model))
        subprocess.run([str(arguments.scanner), "--compare", str(root), str(expected)], check=True)
        print(f"Both indexers agree on {len(tracks)} tracks and one music track")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
