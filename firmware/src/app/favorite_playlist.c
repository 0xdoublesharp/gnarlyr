#include "favorite_playlist.h"

#include "core/i18n.h"
#include "hal/storage.h"
#include "library/playlist_reader.h"
#include "player.h"

const char FAVORITE_PLAYLIST_PATH[] = "/Favorite Tracks.m3u";
const char FAVORITE_ADDED_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/favorite_added.bin";

static const char NAME[] = "Favorite Tracks";

enum {
    NAME_CAPACITY = sizeof NAME + 1
};

typedef struct Added {
    uint32_t fingerprint;
    uint16_t count;
    uint16_t tracks[LIBRARY_EXTRA_CAPACITY];
} Added;

static LibraryExtraPlaylist extra;
static uint16_t indexed;

static bool listed(uint16_t track)
{
    for (uint16_t index = 0; index < extra.count; index++) {
        if (extra.tracks[index] == track) {
            return true;
        }
    }
    return false;
}

static void list(uint16_t track)
{
    if (!listed(track) && extra.count < LIBRARY_EXTRA_CAPACITY) {
        extra.tracks[extra.count++] = track;
    }
}

static bool named_favorites(const char *name)
{
    uint32_t index = 0;
    while (name[index] == NAME[index] && NAME[index] != '\0') {
        index++;
    }
    return name[index] == NAME[index];
}

static bool is_favorites(const Library *library, uint32_t playlist, LibraryGroup *group)
{
    char name[NAME_CAPACITY];
    return library_read_group(library, LIBRARY_PLAYLISTS, playlist, group) &&
           library_read_string(library, group->name, name, sizeof name) && named_favorites(name);
}

static bool copy_indexed(const Library *library)
{
    uint32_t count = library_count(library, LIBRARY_PLAYLISTS);
    LibraryGroup group;
    extra.playlist = (uint16_t)count;
    for (uint32_t playlist = 0; playlist < count; playlist++) {
        if (is_favorites(library, playlist, &group)) {
            extra.playlist = (uint16_t)playlist;
            for (uint16_t link = 0; link < group.link_count; link++) {
                uint16_t track = 0;
                library_read_index(library, LIBRARY_LINKS, group.first_link + link, &track);
                list(track);
            }
            return group.link_count <= LIBRARY_EXTRA_CAPACITY;
        }
    }
    return true;
}

void favorite_playlist_attach(Library *library)
{
    Added added;
    extra.name = NAME;
    extra.count = 0;
    library->extra = NULL;
    if (!copy_indexed(library)) {
        return;
    }
    indexed = extra.count;
    HalFile file;
    bool read = hal_file_open(&file, FAVORITE_ADDED_PATH);
    if (read) {
        read = file.size >= sizeof added && hal_file_read(file, 0, &added, sizeof added);
        hal_file_close(&file);
    }
    if (read && added.fingerprint == library_fingerprint(library) &&
        added.count <= LIBRARY_EXTRA_CAPACITY) {
        for (uint16_t index = 0; index < added.count; index++) {
            if (added.tracks[index] < library_count(library, LIBRARY_TRACKS)) {
                list(added.tracks[index]);
            }
        }
    }
    library->extra = &extra;
}

void favorite_playlist_remember(const Library *library, uint16_t track)
{
    if (library->extra != &extra || listed(track)) {
        return;
    }
    list(track);
    Added added = {.fingerprint = library_fingerprint(library),
                   .count = (uint16_t)(extra.count - indexed)};
    for (uint16_t index = 0; index < added.count; index++) {
        added.tracks[index] = extra.tracks[indexed + index];
    }
    hal_file_write(FAVORITE_ADDED_PATH, &added, sizeof added);
}

const char *favorite_playlist_message(FavoritePlaylistResult result)
{
    static const StringId MESSAGES[] = {STR_FAVORITE_ADDED, STR_FAVORITE_ALREADY,
                                        STR_FAVORITE_FAILED};
    return i18n_text(MESSAGES[result]);
}
