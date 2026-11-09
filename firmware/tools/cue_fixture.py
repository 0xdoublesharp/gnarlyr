import argparse
import subprocess
import sys
from pathlib import Path

FLAC_TRACKS = (
    '  TRACK 01 AUDIO\r\n    TITLE "Opening"\r\n    PERFORMER "Cue Artist"\r\n'
    "    INDEX 01 00:00:00\r\n"
    '  TRACK 02 AUDIO\r\n    TITLE "Middle Part"\r\n    PERFORMER "Guest"\r\n'
    "    INDEX 00 00:00:60\r\n    INDEX 01 00:01:00\r\n"
    "  TRACK 03 AUDIO\r\n    TITLE Closing\r\n    INDEX 01 00:02:37\r\n"
)
FLAC_CUE = (
    "﻿REM GENRE Electronic\r\nREM DATE 1990\r\nREM COMMENT ExactAudioCopy\r\n"
    'PERFORMER "Cue Artist"\r\nTITLE "Cue Album"\r\nFILE "Cue Album.flac" WAVE\r\n' + FLAC_TRACKS
)
WAV_CUE = (
    'rem date "1984-05-01"\ntitle "Caf\xe9 Second"\nfile "Rip Name.wav" WAVE\n'
    "track 01 audio\n  title First\n  performer Alpha\n  index 01 00:00:00\n"
    "track 02 audio\n  title Hidden\n  performer Beta\n  index 00 00:00:30\n"
    "track 03 audio\n  title Last\n  performer Beta\n  index 01 00:00:30\n"
)
MP3_CUE = (
    'PERFORMER "Mp3 Artist"\nTITLE "Mp3 Album"\nFILE "tracks.mp3" MP3\n'
    "  TRACK 01 AUDIO\n    TITLE \"One\"\n    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n    TITLE \"Two\"\n    INDEX 01 00:00:40\n"
)
MULTI_CUE = (
    'TITLE "Separate Files"\nFILE "a.wav" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n'
    'FILE "b.wav" WAVE\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n'
)
FAVORITES_BYTES = 32 * 1024
FAVORITES = (
    "#EXTM3U\n"
    "#EXTINF:2,Middle Part\n#PODIUM-START:1000\nCue Artist/Cue Album/Cue Album.flac\n"
    "#EXTINF:1,Opening\n#PODIUM-START:0\nCue Artist/Cue Album/Cue Album.flac\n"
    "Cue Artist/Cue Album/Cue Album.flac\n"
    "#EXTINF:2,Closing\n#PODIUM-START:2493\nCue Artist/Cue Album/Cue Album.flac\n"
    "#PODIUM-START:7\nCue Artist/Cue Album/Cue Album.flac\n"
    "#PODIUM-START:1000\nPlain/Unsupported/a.wav\n"
)
LEGACY_FAVORITES = "#EXTM3U\nCue Artist/Cue Album/Cue Album.flac\n"
ORPHAN_CUE = 'FILE "missing.flac" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n'


def tone(output, seconds, rate, codec):
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                    f"sine=frequency=440:sample_rate={rate}:duration={seconds}", "-ac", "1",
                    "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                    *codec, str(output)], check=True)


def write_flac(folder):
    folder.mkdir(parents=True)
    tone(folder / "Cue Album.flac", 4, 44100, ["-c:a", "flac"])
    from mutagen.flac import FLAC
    audio = FLAC(folder / "Cue Album.flac")
    audio.clear()
    audio["artist"] = "Tag Artist"
    audio["album"] = "Tagged Album"
    audio["genre"] = "Tag Genre"
    audio["date"] = "1999"
    audio.save()
    (folder / "Cue Album.cue").write_bytes(FLAC_CUE.encode("utf-8"))


def write_wav(folder):
    folder.mkdir(parents=True)
    tone(folder / "Second.wav", 1, 8000, ["-c:a", "pcm_s16le"])
    (folder / "Second.cue").write_bytes(WAV_CUE.encode("cp1252"))


def write_mp3(folder):
    folder.mkdir(parents=True)
    tone(folder / "tracks.mp3", 2, 44100, ["-c:a", "libmp3lame", "-b:a", "64k"])
    (folder / "disc.cue").write_bytes(MP3_CUE.encode("utf-8"))


def write_unsupported(folder):
    folder.mkdir(parents=True)
    for name in ("a.wav", "b.wav"):
        tone(folder / name, 1, 8000, ["-c:a", "pcm_s16le"])
    (folder / "split.cue").write_bytes(MULTI_CUE.encode("utf-8"))
    (folder / "nothing.cue").write_bytes(ORPHAN_CUE.encode("utf-8"))


def write_favorites(output):
    for name, text in (("Favorite Tracks.m3u", FAVORITES), ("Legacy Favorites.m3u", LEGACY_FAVORITES)):
        (output / name).write_bytes(text.encode("utf-8").ljust(FAVORITES_BYTES, b" "))


def parse_arguments():
    parser = argparse.ArgumentParser(description="Write the CUE sheet fixture music folder")
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main():
    output = parse_arguments().output
    write_flac(output / "Cue Artist" / "Cue Album")
    write_wav(output / "Cue Artist" / "Second")
    write_mp3(output / "Mp3 Artist" / "Mp3 Album")
    write_unsupported(output / "Plain" / "Unsupported")
    write_favorites(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
