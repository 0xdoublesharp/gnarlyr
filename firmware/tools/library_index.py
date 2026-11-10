import argparse
import posixpath
import re
import struct
import sys
import unicodedata
from collections import Counter
from dataclasses import dataclass, field, replace
from pathlib import Path, PurePosixPath
from typing import NamedTuple, Optional

from cue_sheet import ascii_fold, parse_cue
from identity import IDENTITY

MAGIC = b"DSDB"
VERSION = 6
HEADER_SIZE = 128
SECTION_ALIGNMENT = 4
NONE = 0xFFFF
MAX_U8 = 0xFF
MAX_U16 = 0xFFFF
MAX_U32 = 0xFFFFFFFF
UNKNOWN_ARTIST = "Unknown Artist"
UNKNOWN_ALBUM = "Unknown Album"
UNKNOWN_GENRE = "Unknown Genre"
AUDIO_SUFFIXES = {".flac", ".ogg", ".mp3", ".m4a", ".m4b", ".wav", ".wma", ".ape", ".dsf", ".dff"}
PLAYLIST_SUFFIXES = {".m3u", ".m3u8"}
CUE_SUFFIX = ".cue"
CUE_AUDIO_SUFFIXES = {".flac", ".wav", ".mp3"}
PATH_SEPARATOR = re.compile(r"[\\/]")
URL_MARKER = "://"
SPOKEN_FOLDERS = {b"audiobooks", b"audiobook", b"podcasts", b"podcast"}

HEADER_PREFIX = struct.Struct("<4sHH")
SECTION_ENTRY = struct.Struct("<IIHH")
TRACK_RECORD = struct.Struct("<IIHHHHIBBHII")
ALBUM_RECORD = struct.Struct("<IHHHHHH")
GROUP_RECORD = struct.Struct("<IIHH")
FOLDER_RECORD = struct.Struct("<IHHIHH")
FOLDER_SHEET = 1
FOLDER_CHAPTERS = 2
INDEX_RECORD = struct.Struct("<H")

START_DIRECTIVE_TEXT = f"#{IDENTITY.playlist_start_directive}:"
START_DIRECTIVE = re.compile(re.escape(START_DIRECTIVE_TEXT) + r"([0-9]{1,10})")
NO_START = 0xFFFFFFFF
LEADING_ARTICLE = re.compile(r"^the\s+(?=\S)")
DISC_TRACK_PREFIX = re.compile(r"^(\d+)-(\d+)\s*-\s*")
TRACK_PREFIX = re.compile(r"^(\d+)\s*[-.]\s*")
LEADING_NUMBER = re.compile(r"\s*(\d+)")
LEADING_YEAR = re.compile(r"\s*(\d{4})")


@dataclass
class TrackInfo:
    path: str
    title: str
    artist: str
    album_artist: str
    album: str
    genre: str
    year: int
    disc: int
    number: int
    duration_ms: int
    album_artist_guessed: bool = False
    start_ms: int = 0
    end_ms: int = 0
    sheet: str = ""
    part: int = 0
    chapters: bool = False


@dataclass
class Album:
    title: str
    album_artist: int
    genre: int
    year: int
    first_track: int = 0
    track_count: int = 0
    spoken: int = 0


@dataclass
class Group:
    name: str
    albums: list = field(default_factory=list)


@dataclass
class Folder:
    name: str
    parent: int
    subfolders: list = field(default_factory=list)
    tracks: list = field(default_factory=list)
    flags: int = 0


class PlaylistEntry(NamedTuple):
    path: str
    start: Optional[int] = None


@dataclass
class RawPlaylist:
    name: str
    folder: str
    entries: list


@dataclass
class Playlist:
    name: str
    tracks: list = field(default_factory=list)
    unresolved: int = 0


@dataclass
class LibraryModel:
    tracks: list
    track_albums: list
    track_artists: list
    track_genres: list
    track_folders: list
    albums: list
    artists: list
    album_artists: list
    genres: list
    folders: list
    title_order: list
    playlists: list = field(default_factory=list)
    music_tracks: int = 0
    music_albums: int = 0
    music_artists: int = 0
    music_album_artists: int = 0
    music_genres: int = 0


def sort_key(text):
    decomposed = unicodedata.normalize("NFKD", text)
    stripped = "".join(character for character in decomposed if not unicodedata.combining(character))
    return LEADING_ARTICLE.sub("", stripped.casefold().strip())


def parse_number(text):
    match = LEADING_NUMBER.match(text)
    return int(match.group(1)) if match else 0


def parse_year(text):
    match = LEADING_YEAR.match(text)
    return int(match.group(1)) if match else 0


def numbers_from_filename(stem):
    disc_track = DISC_TRACK_PREFIX.match(stem)
    if disc_track:
        return int(disc_track.group(1)), int(disc_track.group(2))
    track = TRACK_PREFIX.match(stem)
    return (0, int(track.group(1))) if track else (0, 0)


def title_from_filename(stem):
    for pattern in (DISC_TRACK_PREFIX, TRACK_PREFIX):
        match = pattern.match(stem)
        if match:
            return stem[match.end():]
    return stem


def first_tag(tags, key):
    for value in tags.get(key, []):
        if str(value).strip():
            return str(value).strip()
    return ""


def track_from_tags(relative_path, tags, duration_seconds):
    path = PurePosixPath(relative_path)
    file_disc, file_number = numbers_from_filename(path.stem)
    artist = first_tag(tags, "artist")
    album_artist = first_tag(tags, "albumartist")
    return TrackInfo(
        path=relative_path,
        title=first_tag(tags, "title") or title_from_filename(path.stem),
        artist=artist or album_artist or UNKNOWN_ARTIST,
        album_artist=album_artist or artist or UNKNOWN_ARTIST,
        album=first_tag(tags, "album") or path.parent.name or UNKNOWN_ALBUM,
        genre=first_tag(tags, "genre") or UNKNOWN_GENRE,
        year=parse_year(first_tag(tags, "date")),
        disc=min(parse_number(first_tag(tags, "discnumber")) or file_disc or 1, MAX_U8),
        number=min(parse_number(first_tag(tags, "tracknumber")) or file_number, MAX_U8),
        duration_ms=min(round(duration_seconds * 1000), MAX_U32),
        album_artist_guessed=not album_artist,
    )


def nfc(text):
    return unicodedata.normalize("NFC", text)


FIELDS = ("title", "artist", "albumartist", "album", "genre", "date", "discnumber", "tracknumber")
ID3_FRAMES = ("TIT2", "TPE1", "TPE2", "TALB", "TCON", "TDRC", "TPOS", "TRCK")
MP4_ATOMS = ("\xa9nam", "\xa9ART", "aART", "\xa9alb", "\xa9gen", "\xa9day", "disk", "trkn")
APE_KEYS = ("title", "artist", "album artist", "album", "genre", "year", "disc", "track")
ASF_KEYS = ("Title", "Author", "WM/AlbumArtist", "WM/AlbumTitle", "WM/Genre", "WM/Year",
            "WM/PartOfSet", "WM/TrackNumber")


def id3_values(tags):
    values = {}
    for field_name, frame_id in zip(FIELDS, ID3_FRAMES):
        frame = tags.get(frame_id)
        if frame is not None:
            values[field_name] = frame.genres if frame_id == "TCON" else [str(text) for text in frame.text]
    return values


def mp4_text(value):
    return str(value[0]) if isinstance(value, tuple) else str(value)


def mp4_values(tags):
    values = {}
    for field_name, atom in zip(FIELDS, MP4_ATOMS):
        if atom in tags:
            values[field_name] = [mp4_text(value) for value in tags[atom]]
    return values


def ape_values(tags):
    by_key = {key.lower(): value for key, value in tags.items()}
    values = {}
    for field_name, key in zip(FIELDS, APE_KEYS):
        value = by_key.get(key)
        if value is not None and value.kind == 0:
            values[field_name] = list(value)
    return values


def asf_values(tags):
    values = {}
    for field_name, key in zip(FIELDS, ASF_KEYS):
        if key in tags:
            values[field_name] = [str(value) for value in tags[key]]
    return values


def vorbis_values(tags):
    values = {}
    for key, value in tags:
        values.setdefault(key.lower(), []).append(value)
    return {field_name: values[field_name] for field_name in FIELDS if field_name in values}


def raw_tag_values(tags):
    from mutagen.apev2 import APEv2
    from mutagen.asf import ASFTags
    from mutagen.id3 import ID3
    from mutagen.mp4 import MP4Tags
    if isinstance(tags, ID3):
        return id3_values(tags)
    if isinstance(tags, MP4Tags):
        return mp4_values(tags)
    if isinstance(tags, APEv2):
        return ape_values(tags)
    if isinstance(tags, ASFTags):
        return asf_values(tags)
    return vorbis_values(tags)


def normalized_tags(audio):
    if audio is None or audio.tags is None:
        return {}
    return {key: [nfc(value) for value in values] for key, values in raw_tag_values(audio.tags).items()}


def mp4_metadata(handle):
    from mutagen.mp4 import MP4

    class MetadataMP4(MP4):
        MP4Chapters = staticmethod(lambda *_args: None)

    return MetadataMP4(handle)


def read_track(root, path):
    from mutagen import File
    from tag_skim import open_skimming
    with open_skimming(path) as handle:
        audio = mp4_metadata(handle) if path.suffix.lower() == ".m4b" else File(handle)
    duration = audio.info.length if audio is not None else 0.0
    return track_from_tags(nfc(path.relative_to(root).as_posix()), normalized_tags(audio), duration)


def parse_m3u(text):
    entries = []
    start = None
    for line in (line.strip() for line in text.splitlines()):
        directive = START_DIRECTIVE.fullmatch(line)
        if directive and int(directive.group(1)) < NO_START:
            start = int(directive.group(1))
        if line and not line.startswith("#"):
            entries.append(PlaylistEntry(line, start))
            start = None
    return entries


def decode_playlist(data):
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


def path_parts(path):
    return [part.casefold() for part in path.split("/") if part not in ("", ".", "..")]


def common_suffix_length(first, second):
    length = 0
    while length < min(len(first), len(second)) and first[-1 - length] == second[-1 - length]:
        length += 1
    return length


class PathIndex:
    def __init__(self, tracks):
        paths = [track.path for track in tracks]
        self.exact = {path.casefold(): index for index, path in enumerate(paths)}
        self.folded = [path.casefold() for path in paths]
        self.starts = {}
        for index, track in enumerate(tracks):
            self.starts[(self.folded[index], track.start_ms)] = index
        self.by_name = {}
        for index, path in enumerate(paths):
            parts = path_parts(path)
            self.by_name.setdefault(parts[-1], []).append((parts, index))

    def resolve(self, entry, playlist_folder):
        index = self.resolve_path(entry.path, playlist_folder)
        if index is None or entry.start is None:
            return index
        key = (self.folded[index], entry.start)
        return self.starts.get(key, index)

    def resolve_path(self, entry, playlist_folder):
        if URL_MARKER in entry:
            return None
        normalized = entry.replace("\\", "/")
        for candidate in (posixpath.join(playlist_folder, normalized), normalized):
            index = self.exact.get(posixpath.normpath(candidate).casefold())
            if index is not None:
                return index
        return self.resolve_by_suffix(path_parts(normalized))

    def resolve_by_suffix(self, parts):
        if not parts:
            return None
        scored = sorted(((common_suffix_length(parts, candidate), index)
                         for candidate, index in self.by_name.get(parts[-1], [])), reverse=True)
        if not scored or (len(scored) > 1 and scored[0][0] == scored[1][0]):
            return None
        return scored[0][1]


def resolve_playlists(raw_playlists, tracks):
    paths = PathIndex(tracks)
    playlists = []
    for raw in raw_playlists:
        playlist = Playlist(raw.name)
        for entry in raw.entries:
            index = paths.resolve(entry, raw.folder)
            if index is None:
                playlist.unresolved += 1
            else:
                playlist.tracks.append(index)
        playlists.append(playlist)
    return sorted(playlists, key=lambda playlist: (sort_key(playlist.name), playlist.name))


def spoken_of(track):
    parts = track.path.lstrip("/").split("/")
    return int((len(parts) > 1 and parts[0].encode("utf-8").lower() in SPOKEN_FOLDERS)
               or track.path.lower().endswith(".m4b"))


class NameIndex:
    def __init__(self, entries):
        displays = {}
        for spoken, name in entries:
            displays.setdefault((spoken, sort_key(name)), name)
        ordered = sorted(displays.items(), key=lambda item: (item[0], item[1]))
        self.names = [display for _, display in ordered]
        self.positions = {key: position for position, (key, _) in enumerate(ordered)}
        self.music = sum(1 for (spoken, _), _ in ordered if not spoken)

    def index_of(self, spoken, name):
        return self.positions[(spoken, sort_key(name))]


def album_key(track):
    return spoken_of(track), sort_key(track.album_artist), sort_key(track.album)


def track_order_key(track):
    return track.disc, track.number, sort_key(track.title)


def build_albums(tracks, album_artists, genres):
    grouped = {}
    for track in tracks:
        grouped.setdefault(album_key(track), []).append(track)
    albums = []
    for members in grouped.values():
        years = [track.year for track in members if track.year]
        genre = Counter(track.genre for track in members).most_common(1)[0][0]
        spoken = spoken_of(members[0])
        albums.append((Album(members[0].album,
                             album_artists.index_of(spoken, members[0].album_artist),
                             genres.index_of(spoken, genre), min(years) if years else 0,
                             spoken=spoken),
                       sorted(members, key=track_order_key)))
    albums.sort(key=lambda pair: (pair[0].spoken, sort_key(pair[0].title),
                                  sort_key(album_artists.names[pair[0].album_artist])))
    return albums


def chronological(albums, indices):
    return sorted(indices, key=lambda index: (albums[index].year, sort_key(albums[index].title)))


def build_groups(index, albums, album_tracks, membership, order):
    groups = [Group(name) for name in index.names]
    for album_position, tracks in enumerate(album_tracks):
        for group_position in sorted({index.index_of(spoken_of(track), membership(track))
                                      for track in tracks}):
            groups[group_position].albums.append(album_position)
    for group in groups:
        group.albums = order(albums, group.albums)
    return groups


def folder_node():
    return {"children": {}, "tracks": [], "flags": 0}


def build_folders(tracks):
    tree = folder_node()
    for position, track in enumerate(tracks):
        node = tree
        for part in PurePosixPath(track.path).parts[:-1]:
            node = node["children"].setdefault(part, folder_node())
        if track.part == 0 and not track.chapters:
            node["tracks"].append(position)
        if track.sheet:
            sheet = node["children"].setdefault(track.sheet, folder_node())
            sheet["flags"] |= FOLDER_SHEET | (FOLDER_CHAPTERS if track.chapters else 0)
            sheet["tracks"].append(position)
    folders = []
    add_folder(folders, tree, "", NONE, tracks)
    return folders


def folder_tracks(node, tracks):
    if node["flags"] & FOLDER_SHEET:
        return sorted(node["tracks"], key=lambda index: tracks[index].part)
    return sorted(node["tracks"], key=lambda index: (
        tracks[index].disc, tracks[index].number, sort_key(PurePosixPath(tracks[index].path).name)))


def add_folder(folders, node, name, parent, tracks):
    position = len(folders)
    folder = Folder(name, parent, flags=node["flags"])
    folders.append(folder)
    for child_name in sorted(node["children"], key=lambda child: (sort_key(child), child)):
        folder.subfolders.append(add_folder(folders, node["children"][child_name], child_name,
                                            position, tracks))
    folder.tracks = folder_tracks(node, tracks)
    return position


def folder_of_tracks(folders, track_count):
    owners = [0] * track_count
    for position, folder in enumerate(folders):
        for track in folder.tracks:
            owners[track] = folder.parent if folder.flags & FOLDER_SHEET else position
    return owners


def alphabetical(albums, indices):
    return sorted(indices, key=lambda index: sort_key(albums[index].title))


def order_tracks(album_pairs):
    tracks, track_albums = [], []
    for position, (album, members) in enumerate(album_pairs):
        album.first_track, album.track_count = len(tracks), len(members)
        tracks.extend(members)
        track_albums.extend([position] * len(members))
    return tracks, track_albums


def title_order(tracks):
    return sorted(range(len(tracks)), key=lambda index: (
        spoken_of(tracks[index]), sort_key(tracks[index].title), sort_key(tracks[index].artist),
        index))


def name_index(track_infos, name_of):
    return NameIndex((spoken_of(track), name_of(track)) for track in track_infos)


def guess_album_artists(track_infos):
    groups = {}
    for index, track in enumerate(track_infos):
        if track.album_artist_guessed:
            folder = str(PurePosixPath(track.path).parent)
            groups.setdefault((folder, sort_key(track.album)), []).append(index)
    guessed = list(track_infos)
    for members in groups.values():
        votes = Counter(sort_key(track_infos[index].artist) for index in members)
        best = min(votes, key=lambda key: (-votes[key], key))
        first = next(index for index in members if sort_key(track_infos[index].artist) == best)
        for index in members:
            guessed[index] = replace(track_infos[index], album_artist=track_infos[first].artist,
                                     album_artist_guessed=False)
    return guessed


def build_library(track_infos, raw_playlists=()):
    track_infos = guess_album_artists(track_infos)
    album_artists = name_index(track_infos, lambda track: track.album_artist)
    artists = name_index(track_infos, lambda track: track.artist)
    genres = name_index(track_infos, lambda track: track.genre)
    album_pairs = build_albums(track_infos, album_artists, genres)
    albums = [album for album, _ in album_pairs]
    album_tracks = [members for _, members in album_pairs]
    tracks, track_albums = order_tracks(album_pairs)
    folders = build_folders(tracks)
    return LibraryModel(
        tracks=tracks,
        track_albums=track_albums,
        track_artists=[artists.index_of(spoken_of(track), track.artist) for track in tracks],
        track_genres=[genres.index_of(spoken_of(track), track.genre) for track in tracks],
        track_folders=folder_of_tracks(folders, len(tracks)),
        albums=albums,
        artists=build_groups(artists, albums, album_tracks, lambda t: t.artist, chronological),
        album_artists=build_groups(album_artists, albums, album_tracks,
                                   lambda t: t.album_artist, chronological),
        genres=build_groups(genres, albums, album_tracks, lambda t: t.genre, alphabetical),
        folders=folders,
        title_order=title_order(tracks),
        playlists=resolve_playlists(raw_playlists, tracks),
        music_tracks=sum(1 for track in tracks if not spoken_of(track)),
        music_albums=sum(1 for album in albums if not album.spoken),
        music_artists=artists.music,
        music_album_artists=album_artists.music,
        music_genres=genres.music,
    )


class StringTable:
    def __init__(self):
        self.blob = bytearray(b"\0")
        self.offsets = {"": 0}

    def add(self, text):
        if text not in self.offsets:
            self.offsets[text] = len(self.blob)
            self.blob += text.encode("utf-8") + b"\0"
        return self.offsets[text]


def checked_index(value):
    if value > MAX_U16:
        raise ValueError(f"{value} does not fit the 16-bit index format")
    return value


def encode_tracks(model, strings):
    data = bytearray()
    for position, track in enumerate(model.tracks):
        data += TRACK_RECORD.pack(strings.add(track.title), strings.add(track.path),
                                  model.track_albums[position], model.track_artists[position],
                                  model.track_genres[position], model.track_folders[position],
                                  track.duration_ms, track.disc, track.number, 0,
                                  track.start_ms, track.end_ms)
    return data


def encode_albums(model, strings):
    data = bytearray()
    for album in model.albums:
        data += ALBUM_RECORD.pack(strings.add(album.title), album.album_artist, album.genre,
                                  min(album.year, MAX_U16), album.first_track, album.track_count,
                                  NONE)
    return data


def encode_groups(groups, strings, links):
    data = bytearray()
    for group in groups:
        data += GROUP_RECORD.pack(strings.add(group.name), len(links), len(group.albums), 0)
        links.extend(group.albums)
    return data


def encode_folders(folders, strings, links):
    data = bytearray()
    for folder in folders:
        data += FOLDER_RECORD.pack(strings.add(folder.name), folder.parent,
                                   len(folder.subfolders), len(links), len(folder.tracks),
                                   folder.flags)
        links.extend(folder.subfolders + folder.tracks)
    return data


def encode_playlists(playlists, strings, links):
    data = bytearray()
    for playlist in playlists:
        data += GROUP_RECORD.pack(strings.add(playlist.name), len(links), len(playlist.tracks), 0)
        links.extend(playlist.tracks)
    return data


def encode_indices(values):
    return b"".join(INDEX_RECORD.pack(checked_index(value)) for value in values)


def encode_sections(model):
    strings = StringTable()
    links = []
    sections = [
        (encode_tracks(model, strings), len(model.tracks), TRACK_RECORD.size,
         model.music_tracks),
        (encode_albums(model, strings), len(model.albums), ALBUM_RECORD.size,
         model.music_albums),
        (encode_groups(model.artists, strings, links), len(model.artists), GROUP_RECORD.size,
         model.music_artists),
        (encode_groups(model.album_artists, strings, links), len(model.album_artists),
         GROUP_RECORD.size, model.music_album_artists),
        (encode_groups(model.genres, strings, links), len(model.genres), GROUP_RECORD.size,
         model.music_genres),
        (encode_folders(model.folders, strings, links), len(model.folders), FOLDER_RECORD.size,
         0),
        (encode_indices(model.title_order), len(model.title_order), INDEX_RECORD.size,
         model.music_tracks),
        (encode_playlists(model.playlists, strings, links), len(model.playlists),
         GROUP_RECORD.size, 0),
    ]
    sections.append((encode_indices(links), len(links), INDEX_RECORD.size, 0))
    sections.append((bytes(strings.blob), len(strings.blob), 1, 0))
    return sections


def encode_library(model):
    for count in (len(model.tracks), len(model.albums), len(model.folders)):
        checked_index(count)
    sections = encode_sections(model)
    header = bytearray(HEADER_PREFIX.pack(MAGIC, VERSION, len(sections)))
    body = bytearray()
    for data, count, record_size, music in sections:
        body += bytes(-len(body) % SECTION_ALIGNMENT)
        header += SECTION_ENTRY.pack(HEADER_SIZE + len(body), count, record_size, music)
        body += data
    return bytes(header) + bytes(HEADER_SIZE - len(header)) + bytes(body)


def visible_files(root, suffixes):
    for path in sorted(root.rglob("*")):
        hidden = any(part.startswith(".") for part in path.relative_to(root).parts)
        if path.is_file() and not hidden and path.suffix.lower() in suffixes:
            yield path


def read_playlist(root, path):
    folder = nfc(path.parent.relative_to(root).as_posix())
    entries = [PlaylistEntry(nfc(entry.path), entry.start)
               for entry in parse_m3u(decode_playlist(path.read_bytes()))]
    return RawPlaylist(nfc(path.stem), "" if folder == "." else folder, entries)


def read_cue(path):
    try:
        return parse_cue(decode_playlist(path.read_bytes()))
    except OSError:
        return None


def cue_audio(sheet, cue_path, candidates):
    wanted = ascii_fold(nfc(PATH_SEPARATOR.split(sheet.file)[-1]))
    stem = ascii_fold(nfc(cue_path.stem))
    for path in candidates:
        if ascii_fold(nfc(path.name)) == wanted:
            return path
    for path in candidates:
        if ascii_fold(nfc(path.stem)) == stem:
            return path
    return None


def cue_sheets(files):
    siblings = {}
    for path in files:
        if path.suffix.lower() in CUE_AUDIO_SUFFIXES:
            siblings.setdefault(path.parent, []).append(path)
    sheets = {}
    for path in files:
        sheet = read_cue(path) if path.suffix.lower() == CUE_SUFFIX else None
        audio = cue_audio(sheet, path, siblings.get(path.parent, [])) if sheet else None
        if audio is not None:
            sheets[path] = (sheet, audio)
    return sheets


def cue_track(base, sheet, track, end_ms):
    limit = end_ms or base.duration_ms
    return replace(
        base,
        title=track.title or f"Track {track.number}",
        artist=track.performer or sheet.performer or base.artist,
        album_artist=sheet.performer or base.album_artist,
        album=sheet.title or base.album,
        genre=sheet.genre or base.genre,
        year=parse_year(sheet.date) or base.year,
        number=min(track.number, MAX_U8),
        duration_ms=max(limit - track.start_ms, 0),
        start_ms=track.start_ms,
        end_ms=end_ms,
        album_artist_guessed=base.album_artist_guessed and not sheet.performer,
    )


def cue_tracks(root, sheet, audio_path, cue_path):
    base = replace(read_track(root, audio_path), sheet=nfc(cue_path.name))
    starts = sheet.tracks
    ends = [track.start_ms for track in starts[1:]] + [0]
    return [replace(cue_track(base, sheet, track, end), part=part)
            for part, (track, end) in enumerate(zip(starts, ends))]


def audio_tracks(root, path):
    base = read_track(root, path)
    if path.suffix.lower() != ".m4b":
        return [base]
    from mp4_chapters import read_chapters
    with path.open("rb") as handle:
        chapters = read_chapters(handle, path.stat().st_size, base.duration_ms)
    if not chapters:
        return [base]
    ends = [chapter.start_ms for chapter in chapters[1:]] + [0]
    return [replace(base, title=nfc(chapter.title), number=part + 1, part=part,
                    start_ms=chapter.start_ms, end_ms=end,
                    duration_ms=max((end or base.duration_ms) - chapter.start_ms, 0),
                    sheet=nfc(path.stem), chapters=True)
            for part, (chapter, end) in enumerate(zip(chapters, ends))]


def scan(root):
    tracks, failures = [], []
    files = list(visible_files(root, AUDIO_SUFFIXES | {CUE_SUFFIX}))
    sheets = cue_sheets(files)
    claimed = {audio for _, audio in sheets.values()}
    for path in files:
        if path in claimed or (path.suffix.lower() == CUE_SUFFIX and path not in sheets):
            continue
        try:
            if path in sheets:
                tracks.extend(cue_tracks(root, sheets[path][0], sheets[path][1], path))
            else:
                tracks.extend(audio_tracks(root, path))
        except Exception as error:
            failures.append(f"{path.relative_to(root)}: {error}")
    playlists = [read_playlist(root, path) for path in visible_files(root, PLAYLIST_SUFFIXES)]
    return tracks, playlists, failures


def parse_arguments():
    parser = argparse.ArgumentParser(description=f"Index a music folder into a {IDENTITY.name} library")
    parser.add_argument("--music", type=Path, required=True, help="music root (read only)")
    parser.add_argument("--output", type=Path, required=True, help="library.db to write")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    tracks, playlists, failures = scan(arguments.music.resolve())
    for failure in failures:
        print(f"skipped {failure}", file=sys.stderr)
    model = build_library(tracks, playlists)
    for playlist in model.playlists:
        if playlist.unresolved:
            print(f"playlist {playlist.name}: {playlist.unresolved} entries not found",
                  file=sys.stderr)
    data = encode_library(model)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(data)
    print(f"{len(model.tracks)} tracks, {len(model.albums)} albums, {len(model.artists)} artists, "
          f"{len(model.album_artists)} album artists, {len(model.genres)} genres, "
          f"{len(model.folders)} folders, {len(model.playlists)} playlists, "
          f"{len(data):,} bytes -> {arguments.output}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
