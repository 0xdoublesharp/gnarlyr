"""Small synthetic seed files for long fuzz campaigns, generated with ffmpeg and the theme-pack builder."""
import json
from pathlib import Path
import shutil
import subprocess

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
