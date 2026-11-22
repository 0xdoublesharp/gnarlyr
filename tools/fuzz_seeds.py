"""Fuzz seeds: the hand-written seeds and fixture files every fuzz run starts from, and the small synthetic
media files long campaigns add, generated with ffmpeg and the theme-pack builder."""
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware"
TARGETS = {
    "track_tags": 65536,
    "timed_metadata": 262144,
    "playlist_reader": 32768,
    "session_journal": 4096,
    "library_database": 65536,
    "mp4_demux": 262144,
    "store": 4096,
    "mix_catalog": 256,
    "settings_codec": 128,
    "cue_sheet": 65536,
    "discovery_screen": 256,
    "personal_tags": 320,
    "browse_screen": 256,
    "app_navigation": 128,
    "app_lifecycle": 160,
    "library_builder": 512,
    "cover_image": 1048576,
    "theme_pack": 4096,
    "art_codec": 1030,
    "placeholder_art": 256,
    "play_queue": 256,
    "mp3_probe": 262144,
    "ogg_track": 196608,
    "wav_source": 262144,
    "vorbis_decoder": 196608,
    "opus_decode": 2048,
    "audio_engine": 256,
    "asf_track": 262144,
    "ape_track": 262144,
    "dsd_track": 262144,
    "card_format": 64,
    "playback_views": 256,
    "cover_flow": 256,
    "fat_volume": 400000,
}


def library_builder_fault_seeds():
    tracks = bytes(range(248)) * 2
    seeds = []
    for field in range(6):
        playlist_modes = (0, 8) if field >= 3 else (0,)
        for playlist in playlist_modes:
            for value in range(1, 256):
                controls = bytearray(8)
                controls[field] = value
                controls[6] = playlist
                seeds.append(bytes(controls) + tracks)
    return seeds


ASF_HEADER = bytes.fromhex("3026b2758e66cf11a6d900aa0062ce6c")
ASF_EXTENSION = bytes.fromhex("b503bf5f2ea9cf118ee300c00c205365")
ASF_METADATA_LIBRARY = bytes.fromhex("941c23449894d149a1411d134e457054")


def asf_metadata_library(name, value):
    name_units = (name + "\0").encode("utf-16-le")
    value_units = (value + "\0").encode("utf-16-le")
    record = struct.pack("<HHHHI", 0, 0, len(name_units), 0, len(value_units)) + name_units + value_units
    body = struct.pack("<H", 1) + record
    return ASF_METADATA_LIBRARY + struct.pack("<Q", 24 + len(body)) + body


def nested_asf_seed(levels):
    body = asf_metadata_library("WM/Genre", "Deep")
    for _ in range(levels):
        body = ASF_EXTENSION + struct.pack("<Q", 46 + len(body)) + bytes(18) + struct.pack("<I", len(body)) + body
    tagged = body + asf_metadata_library("WM/AlbumArtist", "Art")
    top = ASF_EXTENSION + struct.pack("<Q", 46 + len(tagged)) + bytes(18) + struct.pack("<I", len(tagged)) + tagged
    return ASF_HEADER + struct.pack("<QIBB", 30 + len(top), 1, 1, 2) + top


SEEDS = {
    "track_tags": [
        b"\0",
        nested_asf_seed(2),
        nested_asf_seed(1000),
        b"ID3\x04\0\0\0\0\0\x02\0\0ID3\x04\0\0\0\0\0\0fLaC\x80\0\0\x22" + bytes(34),
        b"ID3\x04\0\0\0\0\0\x04\0\0\0\0ID3\x04\0\0\x7f\x7f\x7f\x7f" + b"\xff\xfb\x90\x64" + bytes(412),
        b"ID3\x04\0\0\0\0\0\0" + (b"\xff\xfb\x90\x64" + bytes(413)) * 4,
    ],
    "timed_metadata": [
        b"[00:00.00]First line\n[00:01.50][00:02.00]Repeated line\n",
        b"\xef\xbb\xbf[offset:-250]\n[00:00.10]Early\n[9999:59.999]Late\n",
        b"[00:60.00]invalid\n[00:01.]invalid\n[00:01.1234]invalid\n",
    ],
    "playlist_reader": [
        b"\xff#EXTM3U\n../A/b.flac\n",
        b"\x1f\xef\xbb\xbfA/one.flac\xe2\x80\xa8A/two.flac\n",
        b"\x03Bj\xf6rk/J\xf3ga.flac\x85end.flac\n",
    ],
    "session_journal": [b"\0", bytes(range(64)), b"\xff" * 64],
    "library_database": [b"\0", b"\0" * 128],
    "mp4_demux": [b"\0"],
    "store": [
        b"\0" * 16,
        bytes(range(32)),
        b"\xff\x01\xff\x01\0\x02\0\x02" * 4,
    ],
    "mix_catalog": [bytes(range(128)), b"\0" * 64, b"\xff\x80\x40\x20" * 32],
    "settings_codec": [
        bytes(range(128)),
        b"\1" + bytes(range(32)),
        b"\2" + bytes(range(64)),
        b"\3" + b"\xff" * 32,
    ],
    "cue_sheet": [
        b'FILE "a.flac" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n'
        b"TRACK 02 AUDIO\nINDEX 01 03:20:00\n",
        b"FILE a.flac WAVE\nTRACK 00 AUDIO\nINDEX 01 00:00:00\n"
        b"TRACK 100 AUDIO\nINDEX 01 00:01:00\nTRACK AUDIO\nINDEX 01 00:02:00\n",
        b"FILE a.flac WAVE\nTRACK 01 AUDIO\nINDEX 01 00:60:00\n"
        b"TRACK 02 AUDIO\nINDEX 01 00:00:75\nTRACK 03 AUDIO\nINDEX 01 00:00:00 trailing\n",
        b"FILE a.flac WAVE\nTRACK 01 AUDIO\nINDEX 01 00:10:00\n"
        b"TRACK 02 AUDIO\nINDEX 01 00:05:00\nTRACK 03 AUDIO\nINDEX 01 00:10:00\n",
    ],
    "discovery_screen": [
        bytes(range(96)),
        b"\0\1" + bytes([0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0]) * 12,
        b"\7\2" + bytes([2, 7, 12, 17, 22, 27, 31, 63, 95, 127, 159, 191, 223, 255]) * 6,
    ],
    "personal_tags": [
        bytes(range(160)),
        b"\0\0\0\0\0\3\0\1\xff\xff\5\0\0\0\0" * 8,
        b"\0\x1f\0\1\2\1\x1f\0\3\4\2\x1f\0\5\6\4\0\0\0\0" * 6,
    ],
    "browse_screen": [
        bytes(range(96)),
        b"\0" + bytes([0, 1, 2, 3, 4, 5, 31, 63, 95, 127, 159, 191, 223, 255]) * 6,
        b"\4" + bytes([2, 7, 12, 17, 22, 27, 32, 37]) * 11,
        bytes([0x40, 0x07, 0x5A, 0x5F, 0x5C]),
    ],
    "app_navigation": [
        bytes(range(48)),
        bytes([0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13]) * 4,
        bytes([0x20, 0x41, 0x62, 0x83, 0xa4, 0xc5, 0xe0, 0xff]) * 6,
    ],
    "app_lifecycle": [
        bytes(range(96)),
        bytes([0, 0x30, 102, 0, 0, 0, 0, 0, 2, 13, 20, 0, 0, 32, 64, 13, 20, 32]),
        bytes([0, 0x30, 13, 17, 32, 0, 32, 5, 32, 32, 34, 64, 13, 17, 32, 34]),
        bytes([0, 0x32, 32, 0, 0, 0, 0, 0, 0, 0, 32, 32, 4, 10, 4, 21, 4, 4]),
        bytes([1, 0x70, 6, 13, 36, 8, 8, 24, 102, 0, 0, 0, 0, 0, 2, 13, 20, 32]),
    ],
    "library_builder": [
        bytes(range(128)),
        b"\0\1\2\3\4\5\6\7" * 32,
        b"\xff\x80\x40\x20\x10\x08\x04\x02" * 32,
        *library_builder_fault_seeds(),
    ],
    "cover_image": [b"\0", b"\xff\xd8\xff\xd9", b"\x89PNG\r\n\x1a\n"],
    "theme_pack": [b"\0", b"\1\0\0\0", b"\2\0\0\0\0\0", b"\3\0\0\0"],
    "art_codec": [b"\x07\0\0\x08\0\0\x87\x01", b"\x07\0\x02\x04\x02\x03\xc1\xe8\x80\xff\xff\x04"],
    "placeholder_art": [
        b"The Velvet Underground",
        "...And Justice for All".encode(),
        "\u6771\u4eac Nights".encode(),
        bytes(range(128)),
    ],
    "play_queue": [bytes(range(64)), b"\0\1\2\3" * 32, b"\xff\x80\x40\x20" * 32],
    "mp3_probe": [b"\0", b"ID3\4\0\0\0\0\0\0", b"\xff\xfb\x90\0"],
    "ogg_track": [b"\0", b"OggS" + bytes(24), bytes(range(128))],
    "wav_source": [b"\0", b"RIFF" + bytes(4) + b"WAVE"],
    "vorbis_decoder": [b"\0", b"OggS" + bytes(24)],
    "opus_decode": [
        bytes(12) + b"\xf8\xff\xfe",
        bytes([2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255]) + b"\x48\0\0",
        bytes(range(64)),
    ],
    "audio_engine": [bytes(range(128)), b"\0" * 64, b"\xff\x80\x40\x20" * 32],
    "asf_track": [b"\0", bytes(range(128))],
    "ape_track": [b"\0", b"MAC " + bytes(124)],
    "dsd_track": [b"\0", b"DSD " + bytes(124), b"FRM8" + bytes(124)],
    "card_format": [b"\0" * 16, bytes(range(16)), b"\xff\0\0\0" * 4],
    "playback_views": [bytes(range(128)), b"\0" * 64, b"\xff\x80\x40\x20" * 32, b"\x19\x04"],
    "cover_flow": [bytes(range(128)), b"\0" * 64, b"\xff\x80\x40\x20" * 32],
    "fat_volume": [b"\0", b"DSSP" + bytes(8)],
}
FIXTURES = {
    "library_database": [
        SOURCE / "tests/fixtures/small_library.db",
        SOURCE / "tests/fixtures/cue_music.db",
        SOURCE / "tests/fixtures/builder_random/library.db",
        SOURCE / "tests/fixtures/builder_random/with_playlists.db",
    ],
    "mp4_demux": [
        SOURCE / "tests/fixtures/tags/aac.m4a",
        SOURCE / "tests/fixtures/tags/alac.m4a",
        SOURCE / "tests/fixtures/m4a_music/Aac Artist/Aac Album/01 Lead.m4a",
        SOURCE / "tests/fixtures/m4a_music/Aac Artist/Aac Album/02 Tail.m4a",
        SOURCE / "tests/fixtures/m4a_music/Aac Artist/Aac Album/03 Mono.m4a",
        SOURCE / "tests/fixtures/m4a_music/Alac Artist/Alac Album/01 Lossless.m4a",
        SOURCE / "tests/fixtures/m4a_music/Alac Artist/Alac Album/02 Deep.m4a",
        SOURCE / "tests/fixtures/m4a_music/Alac Artist/Alac Album/03 Single.m4a",
    ],
    "timed_metadata": [
        *sorted((SOURCE / "tests/fixtures/m4b_music/Audiobooks").glob("*.m4b")),
        *sorted((SOURCE / "tests/fixtures/m4b_real/Audiobooks").glob("*.m4b")),
    ],
    "cover_image": [
        ROOT / "site/assets/screens/home.png",
        ROOT / "site/assets/screens/cover-flow.png",
        *sorted((ROOT / "site/assets/social").glob("home-*.png")),
    ],
    "mp3_probe": [
        SOURCE / "tests/fixtures/tags/id3v24.mp3",
        SOURCE / "tests/fixtures/tags/id3v23_vbr.mp3",
        SOURCE / "tests/fixtures/tags/id3v1_no_xing.mp3",
    ],
    "ogg_track": [
        *sorted((SOURCE / "tests/fixtures/vorbis").glob("*.ogg")),
        *sorted((SOURCE / "tests/fixtures/opus").glob("*.opus")),
        SOURCE / "tests/fixtures/tags/tagged.ogg",
        SOURCE / "tests/fixtures/tags/tagged.opus",
    ],
    "wav_source": [SOURCE / "tests/fixtures/tags/tagged.wav"],
    "vorbis_decoder": [*sorted((SOURCE / "tests/fixtures/vorbis").glob("*.ogg"))],
    "asf_track": [
        *sorted((SOURCE / "tests/fixtures/wma").glob("*.wma")),
        SOURCE / "tests/fixtures/tags/tagged.wma",
    ],
    "ape_track": [
        *sorted((SOURCE / "tests/fixtures/ape").glob("*.ape")),
        SOURCE / "tests/fixtures/tags/tagged.ape",
    ],
    "dsd_track": [
        SOURCE / "tests/fixtures/tags/tagged.dff",
        SOURCE / "tests/fixtures/tags/tagged.dsf",
    ],
    "fat_volume": [
        SOURCE / "tests/fixtures/cards/fat16_superfloppy.sparse",
        SOURCE / "tests/fixtures/cards/fat32_mbr.sparse",
        SOURCE / "tests/fixtures/cards/exfat_mbr.sparse",
    ],
}
COVER_JPEG_FIXTURES = [
    SOURCE / "tests/fixtures/tags/id3v24.mp3",
    SOURCE / "tests/fixtures/tags/aac.m4a",
    SOURCE / "tests/fixtures/tags/tagged.flac",
]


def inputs(name):
    found = {f"seed-{index}": seed for index, seed in enumerate(SEEDS[name])}
    for fixture in FIXTURES.get(name, ()):
        found[f"fixture-{fixture.name}"] = fixture.read_bytes()[:TARGETS[name]]
    if name == "cover_image":
        for index, fixture in enumerate(COVER_JPEG_FIXTURES):
            bytes_ = fixture.read_bytes()
            start = bytes_.find(b"\xff\xd8")
            end = bytes_.find(b"\xff\xd9", start + 2)
            if start < 0 or end < 0:
                raise SystemExit(f"No JPEG cover found in {fixture}")
            found[f"jpeg-{index}"] = bytes_[start:end + 2]
    if name == "track_tags":
        fixtures = SOURCE / "tests/fixtures/tags"
        for fixture in sorted(fixtures.iterdir()):
            if fixture.is_file():
                found[f"tag-{fixture.name}"] = fixture.read_bytes()[:TARGETS[name]]
    return found


TONE = "sine=frequency=440:sample_rate={rate}:duration={seconds}"
LONG_TITLE = "Long " * 200
TAG_SETS = {
    "plain": {"title": "Seed", "artist": "Fuzz", "album": "Corpus", "track": "3/12", "disc": "1/2",
              "date": "2026-11-19", "genre": "Test"},
    "unicode": {"title": "\u6771\u4eac \U0001f3b5 \u05e9\u05dc\u05d5\u05dd", "artist": "Bj\u00f6rk & J\u00f3ga",
                "album_artist": "\u00c9milie", "album": "\u30a2\u30eb\u30d0\u30e0", "track": "0", "disc": "0/0"},
    "long": {"title": LONG_TITLE, "artist": LONG_TITLE, "album": "", "track": "9999/9999",
             "comment": "x" * 2000, "lyrics": "line\n" * 300},
    "gain": {"title": "Gain", "REPLAYGAIN_TRACK_GAIN": "-6.50 dB", "REPLAYGAIN_ALBUM_GAIN": "+1.25 dB",
             "REPLAYGAIN_TRACK_PEAK": "0.988", "compilation": "1", "album_artist": "Various"},
}
AUDIO = (
    ("flac16", ".flac", ["-c:a", "flac", "-sample_fmt", "s16"], 8000, 0.25, 1),
    ("flac24", ".flac", ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24"], 48000, 0.05, 2),
    ("mp3cbr", ".mp3", ["-c:a", "libmp3lame", "-b:a", "32k", "-id3v2_version", "3"], 8000, 0.5, 1),
    ("mp3vbr", ".mp3", ["-c:a", "libmp3lame", "-q:a", "9", "-id3v2_version", "4", "-write_id3v1", "1"],
     22050, 0.5, 2),
    ("mp3bare", ".mp3", ["-c:a", "libmp3lame", "-b:a", "64k", "-write_xing", "0", "-id3v2_version", "0"],
     44100, 0.2, 2),
    ("vorbis", ".ogg", ["-c:a", "vorbis", "-strict", "-2", "-q:a", "0"], 22050, 0.3, 2),
    ("opus", ".opus", ["-c:a", "libopus", "-b:a", "16k", "-frame_duration", "20"], 48000, 0.3, 1),
    ("opus60", ".opus", ["-c:a", "libopus", "-b:a", "24k", "-frame_duration", "60"], 48000, 0.3, 2),
    ("aac", ".m4a", ["-c:a", "aac", "-b:a", "24k"], 22050, 0.3, 1),
    ("aacfast", ".m4a", ["-c:a", "aac", "-b:a", "24k", "-movflags", "+faststart"], 22050, 0.3, 2),
    ("alac", ".m4a", ["-c:a", "alac"], 8000, 0.2, 1),
    ("wav16", ".wav", ["-c:a", "pcm_s16le"], 8000, 0.2, 1),
    ("wav24", ".wav", ["-c:a", "pcm_s24le"], 44100, 0.05, 2),
    ("wav32", ".wav", ["-c:a", "pcm_s32le"], 48000, 0.03, 2),
    ("wavu8", ".wav", ["-c:a", "pcm_u8"], 8000, 0.2, 1),
    ("wavfloat", ".wav", ["-c:a", "pcm_f32le"], 8000, 0.1, 1),
    ("wavext", ".wav", ["-c:a", "pcm_s16le", "-rf64", "auto", "-write_bext", "1"], 8000, 0.1, 2),
    ("aiff", ".aiff", ["-c:a", "pcm_s16be"], 8000, 0.2, 2),
    ("wma1", ".wma", ["-c:a", "wmav1", "-b:a", "32k"], 22050, 0.3, 1),
    ("wma2", ".wma", ["-c:a", "wmav2", "-b:a", "32k"], 22050, 0.3, 2),
)
ART_CONTAINERS = ("flac16", "mp3cbr", "mp3vbr", "aac", "alac")
IMAGES = (
    ("baseline", ".jpg", ["-c:v", "mjpeg", "-q:v", "8"], "testsrc=size=64x64"),
    ("gray", ".jpg", ["-c:v", "mjpeg", "-pix_fmt", "gray"], "testsrc=size=40x24"),
    ("yuv444", ".jpg", ["-c:v", "mjpeg", "-pix_fmt", "yuvj444p", "-huffman", "0"], "testsrc=size=33x17"),
    ("odd", ".jpg", ["-c:v", "mjpeg", "-pix_fmt", "yuvj420p"], "testsrc=size=1x1"),
    ("restart", ".jpg", ["-c:v", "mjpeg", "-pix_fmt", "yuvj422p", "-slices", "4"], "testsrc=size=48x48"),
    ("rgb", ".png", ["-c:v", "png", "-pix_fmt", "rgb24"], "testsrc=size=32x32"),
    ("rgba", ".png", ["-c:v", "png", "-pix_fmt", "rgba"], "testsrc=size=16x16"),
    ("pal", ".png", ["-c:v", "png", "-pix_fmt", "pal8"], "testsrc=size=24x8"),
    ("gray16", ".png", ["-c:v", "png", "-pix_fmt", "gray16be"], "testsrc=size=8x8"),
    ("rgb48", ".png", ["-c:v", "png", "-pix_fmt", "rgb48be", "-pred", "mixed"], "testsrc=size=9x7"),
)
CUE_SHEETS = {
    "basic.cue": 'PERFORMER "Fuzz"\nTITLE "Corpus"\nFILE "album.flac" WAVE\n  TRACK 01 AUDIO\n    TITLE "One"\n'
                 '    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    TITLE "Two"\n    INDEX 00 03:58:50\n'
                 '    INDEX 01 04:00:00\n',
    "rem.cue": 'REM GENRE Rock\nREM DATE 1999\nREM DISCID 12345678\nREM COMMENT "ExactAudioCopy"\n'
               'REM REPLAYGAIN_ALBUM_GAIN -7.00 dB\nCATALOG 0000000000000\nFILE "a.wav" WAVE\n'
               '  TRACK 01 AUDIO\n    FLAGS DCP\n    ISRC ABCDE1234567\n    PREGAP 00:02:00\n'
               '    INDEX 01 00:00:00\n    POSTGAP 00:01:00\n',
    "multi.cue": 'FILE "1.flac" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\nFILE "2.flac" WAVE\n'
                 '  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:00:32\n',
    "crlf.cue": '\ufeffTITLE "\u6771\u4eac"\r\nFILE "\u00e9t\u00e9.flac" WAVE\r\n  TRACK 01 AUDIO\r\n'
                '    PERFORMER "Bj\u00f6rk"\r\n    INDEX 01 00:00:00\r\n',
    "edge.cue": 'FILE a.flac WAVE\nTRACK 99 AUDIO\nINDEX 01 99:59:74\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n'
                'TITLE ""\nPERFORMER\nINDEX 01\nFILE "unterminated\n',
}
PLAYLISTS = {
    "ext.m3u": "#EXTM3U\n#EXTINF:123,Artist - Title\nMusic/a.flac\n#EXTINF:-1,\n../b.mp3\n",
    "bom.m3u8": "\ufeff#EXTM3U\r\n#PLAYLIST:\u6771\u4eac\r\n\u00e9t\u00e9/\u00fcber.flac\r\n/abs/path.ogg\r\n",
    "windows.m3u": "C:\\Music\\a.flac\r\n..\\b.flac\r\n.\\c.flac\r\n\r\n#comment\r\n",
    "long.m3u": "#EXTM3U\n" + ("d/" * 120) + "x.flac\n" + "y" * 600 + ".mp3\n",
    "urls.m3u": "#EXTM3U\nfile:///Music/a.flac\nhttp://example.invalid/stream\n#EXTALB:A\n#EXTART:B\n",
}
LYRICS = {
    "basic.lrc": "[ti:Seed]\n[ar:Fuzz]\n[offset:+250]\n[00:00.00]First\n[00:01.50][00:03.00]Twice\n",
    "edge.lrc": "\ufeff[offset:-99999]\n[99:59.999]Late\n[00:00]Short\n<00:00.10>word\n[00:60.00]bad\n[length:03:00]\n",
}


def ffmpeg_jobs(directory, tone_rate=8000):
    jobs = []
    for name, suffix, codec, rate, seconds, channels in AUDIO:
        source = ["-f", "lavfi", "-i", TONE.format(rate=rate, seconds=seconds)]
        for tag_name, tags in TAG_SETS.items():
            metadata = [item for key, value in tags.items() for item in ("-metadata", f"{key}={value}")]
            output = directory / f"{name}-{tag_name}{suffix}"
            jobs.append([*source, "-ac", str(channels), *metadata, *codec, output])
    for name, suffix, codec, source in IMAGES:
        jobs.append(["-f", "lavfi", "-i", source, "-frames:v", "1", *codec, directory / f"cover-{name}{suffix}"])
    for name in ART_CONTAINERS:
        _, suffix, codec, rate, seconds, channels = next(entry for entry in AUDIO if entry[0] == name)
        for cover in ("baseline.jpg", "rgb.png"):
            jobs.append(["-f", "lavfi", "-i", TONE.format(rate=rate, seconds=seconds),
                         "-i", directory / f"cover-{cover}", "-map", "0:a", "-map", "1:v", "-ac", str(channels),
                         *codec, "-c:v", "copy", "-disposition:v", "attached_pic",
                         directory / f"{name}-art-{cover.split('.')[0]}{suffix}"])
    chapters = directory / "chapters.ffmeta"
    jobs.append(["-f", "lavfi", "-i", TONE.format(rate=8000, seconds=1), "-i", chapters, "-map_metadata", "1",
                 "-map_chapters", "1", "-c:a", "aac", "-b:a", "16k", directory / "chapters.m4b"])
    return jobs


def chapter_metadata():
    lines = [";FFMETADATA1", "title=Book"]
    for index, (start, end, title) in enumerate(((0, 300, "Opening"), (300, 700, "\u6771\u4eac"),
                                                 (700, 1000, "x" * 300))):
        lines += ["[CHAPTER]", "TIMEBASE=1/1000", f"START={start}", f"END={end}", f"title={title}"]
    return "\n".join(lines) + "\n"


def palette_only_manifest(manifest):
    reduced = dict(manifest)
    reduced.pop("assets", None)
    return reduced


def theme_packs(directory, root, python, run):
    built = []
    for folder in sorted((root / "examples/themes").iterdir()):
        manifest_path = folder / "theme.json"
        if not manifest_path.is_file():
            continue
        palette = directory / f"theme-src-{folder.name}"
        palette.mkdir(exist_ok=True)
        manifest = json.loads(manifest_path.read_text())
        (palette / "theme.json").write_text(json.dumps(palette_only_manifest(manifest)))
        for source, output in ((palette, directory / f"palette-{folder.name}.thm"),
                               (folder, directory / f"full-{folder.name}.thm")):
            run([python, root / "firmware/tools/theme_pack.py", source, "--output", output])
            built.append(output)
    return built


def generate(directory, root, python, env, version):
    stamp = directory / "complete"
    if stamp.is_file() and stamp.read_text() == version:
        return sorted(path for path in directory.iterdir() if path.is_file())
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)

    def run(command):
        subprocess.run([str(part) for part in command], check=True, env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

    (directory / "chapters.ffmeta").write_text(chapter_metadata())
    for files in (CUE_SHEETS, PLAYLISTS, LYRICS):
        for name, text in files.items():
            (directory / name).write_text(text, encoding="utf-8")
    ffmpeg = shutil.which("ffmpeg", path=env.get("PATH"))
    failed = []
    for job in ffmpeg_jobs(directory):
        try:
            run([ffmpeg, "-hide_banner", "-nostdin", "-y", *job])
        except subprocess.CalledProcessError:
            failed.append(Path(job[-1]).name)
    theme_packs(directory, root, python, run)
    (directory / "failed.txt").write_text("\n".join(failed) + ("\n" if failed else ""))
    stamp.write_text(version)
    return sorted(path for path in directory.iterdir() if path.is_file())
