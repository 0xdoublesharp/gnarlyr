from pathlib import Path

from .fat_volume import Fat32Volume
from .flac_image import FRAME_BYTES, HEADER_BYTES, FlacImage

PROJECT = Path(__file__).resolve().parents[2]
CUE_ALBUM = PROJECT / "tests" / "fixtures" / "cue_music" / "Cue Artist" / "Cue Album"
MEGABYTES = 3480
SECTORS_PER_CLUSTER = 8
ALBUM_COUNT = 7
ALBUM_BYTES = 330 * 1024 * 1024
ALBUM_FRAMES = (ALBUM_BYTES - HEADER_BYTES) // FRAME_BYTES
TRACKS_PER_ALBUM = 12
SPOTLIGHT_FILES = 1500
FSEVENTS_FILES = 120
UPDATE_BYTES = 5 * 1024 * 1024
GUID = "0B5C1A2E-6F3D-4E7A-9C41-5D2E8B7A1F30"
ALBUMS = ("Speak and Spell", "A Broken Frame", "Construction Time Again", "Some Great Reward",
          "Black Celebration", "Music for the Masses", "Violator")


def album_cue(album, seconds):
    lines = ["PERFORMER \"Depeche Mode\"", "TITLE \"%s\"" % album,
             "FILE \"%s.flac\" WAVE" % album]
    length = seconds // TRACKS_PER_ALBUM
    for number in range(TRACKS_PER_ALBUM):
        start = number * length
        lines += ["  TRACK %02d AUDIO" % (number + 1),
                  "    TITLE \"%s %02d\"" % (album, number + 1),
                  "    INDEX 01 %02d:%02d:00" % (start // 60, start % 60)]
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def album_files(album, playable):
    if not playable:
        return ((CUE_ALBUM / "Cue Album.flac").read_bytes(), None, ALBUM_BYTES,
                (CUE_ALBUM / "Cue Album.cue").read_bytes())
    source = FlacImage(ALBUM_FRAMES)
    return None, source, None, album_cue(album, source.seconds)


def add_albums(volume, playable=False, album_count=ALBUM_COUNT):
    volume.add_directory("Depeche Mode")
    for index, album in enumerate(ALBUMS[:album_count]):
        path = "Depeche Mode/" + album
        volume.add_directory(album, "Depeche Mode")
        data, source, size, cue = album_files(album, playable)
        volume.add_file(album + ".flac", data, size, path, source)
        volume.add_file(album + ".cue", cue, None, path)
        volume.add_file("folder.jpg", bytes(range(256)) * 400, None, path)
        volume.add_file("rip.log", b"EAC extraction logfile\n" * 40, None, path)
    volume.add_file("Depeche Mode.pdf", None, 3 * 1024 * 1024, "Depeche Mode")
    volume.add_directory("Invisible Nature")
    volume.add_file("folder.jpg", bytes(range(256)) * 300, None, "Invisible Nature")
    volume.add_directory("Massive Attack")


def add_macos_files(volume):
    volume.add_directory(".Spotlight-V100")
    volume.add_directory("Store-V2", ".Spotlight-V100")
    store = ".Spotlight-V100/Store-V2"
    volume.add_directory(GUID, store)
    for index in range(SPOTLIGHT_FILES):
        volume.add_file("%d.directoryStoreFile.shadow" % index, None, 4096, store + "/" + GUID)
    volume.add_directory(".fseventsd")
    for index in range(FSEVENTS_FILES):
        volume.add_file("%016x" % (0x1B7A000 + index), bytes(64), None, ".fseventsd")
    volume.add_directory(".Trashes")
    volume.add_file("._Depeche Mode", bytes(4096), None)
    volume.add_file(".DS_Store", bytes(6148), None)
    volume.add_directory("System Volume Information")
    volume.add_file("IndexerVolumeGuid", bytes(76), None, "System Volume Information")
    volume.add_file("WPSettingsDat", bytes(12), None, "System Volume Information")


def real_volume(update_name=None, update_data=None, sectors_per_cluster=SECTORS_PER_CLUSTER,
                next_free=None, playable=False, album_count=ALBUM_COUNT):
    volume = Fat32Volume(MEGABYTES, sectors_per_cluster, fsinfo=True, next_free=next_free)
    add_albums(volume, playable, album_count)
    add_macos_files(volume)
    volume.add_file("notes.txt", b"liner notes\n" * 100, None)
    if update_name:
        volume.add_file(update_name, update_data, UPDATE_BYTES)
    return volume
