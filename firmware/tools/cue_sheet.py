import re
import unicodedata
from dataclasses import dataclass, field

FRAMES_PER_SECOND = 75
MILLISECONDS_PER_SECOND = 1000
MAX_U32 = 0xFFFFFFFF
LINE = re.compile(r"([^ \t]+)[ \t]*(.*)\Z", re.S)
INDEX = re.compile(r"([0-9]+)[ \t]+([0-9]+):([0-9]+):([0-9]+)[ \t]*\Z")
TRACK_NUMBER = re.compile(r"([0-9]+)(?:[ \t]+.*)?\Z", re.S)
BLANK = re.compile(r"[ \t]")
ASCII_FOLD = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")


@dataclass
class CueTrack:
    number: int
    title: str = ""
    performer: str = ""
    start_ms: int = -1


@dataclass
class CueSheet:
    file: str = ""
    title: str = ""
    performer: str = ""
    genre: str = ""
    date: str = ""
    tracks: list = field(default_factory=list)


def ascii_fold(text):
    return text.translate(ASCII_FOLD)


def cue_value(rest, whole):
    if rest.startswith('"'):
        end = rest.find('"', 1)
        text = rest[1:] if end < 0 else rest[1:end]
    else:
        text = rest if whole else BLANK.split(rest, maxsplit=1)[0]
    return unicodedata.normalize("NFC", text)


def track_number(text):
    match = TRACK_NUMBER.fullmatch(text)
    if match is None:
        return 0
    value = int(match.group(1))
    return value if 1 <= value <= 99 else 0


def index_start(rest):
    match = INDEX.match(rest)
    if match is None or int(match.group(1)) != 1:
        return -1
    minutes, seconds, frames = (int(match.group(part)) for part in (2, 3, 4))
    if seconds >= 60 or frames >= FRAMES_PER_SECOND:
        return -1
    total = (minutes * 60 + seconds) * FRAMES_PER_SECOND + frames
    return min(total * MILLISECONDS_PER_SECOND // FRAMES_PER_SECOND, MAX_U32)


def usable_tracks(tracks):
    usable = []
    for track in tracks:
        if not track.number or track.start_ms < 0:
            continue
        if not usable or track.start_ms > usable[-1].start_ms:
            usable.append(track)
    return usable


def apply_rem(sheet, rest):
    match = LINE.match(rest)
    if match is None:
        return
    keyword = ascii_fold(match.group(1))
    if keyword in ("genre", "date"):
        setattr(sheet, keyword, cue_value(match.group(2), True))


def parse_cue(text):
    sheet = CueSheet()
    files = 0
    track = None
    for line in text.splitlines():
        match = LINE.match(line.strip())
        if match is None:
            continue
        keyword, rest = ascii_fold(match.group(1)), match.group(2)
        if keyword == "file":
            files += 1
            sheet.file = sheet.file or cue_value(rest, False)
        elif keyword == "track":
            track = CueTrack(track_number(rest))
            sheet.tracks.append(track)
        elif keyword in ("title", "performer"):
            setattr(track or sheet, keyword, cue_value(rest, True))
        elif keyword == "index" and track is not None and track.number:
            start = index_start(rest)
            track.start_ms = start if start >= 0 else track.start_ms
        elif keyword == "rem" and track is None:
            apply_rem(sheet, rest)
    sheet.tracks = usable_tracks(sheet.tracks)
    return sheet if files == 1 and sheet.tracks else None
