#include "favorite_playlist.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/storage.h"
#include "library/playlist_reader.h"
#include "player.h"

const char FAVORITE_PLAYLIST_PATH[] = "/Favorite Tracks.m3u";
const char FAVORITE_ADDED_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/favorite_added.bin";

static const char NAME[] = "Favorite Tracks";

enum {
    PLAYLIST_BYTES = 32 * 1024,
    CHUNK_BYTES = 256,
    KEY_CAPACITY = PLAYER_PATH_CAPACITY + 32,
    HEADER_BYTES = 8,
    BLOCK_CAPACITY = HEADER_BYTES + KEY_CAPACITY + 2,
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

static void remember(const Library *library, uint16_t track)
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

typedef struct Contents {
    const char *track;
    uint32_t matched;
    bool equal;
    bool found;
    char previous;
    uint32_t end;
} Contents;

typedef struct Block {
    char text[BLOCK_CAPACITY];
    StringBuilder builder;
} Block;

static void feed(Contents *contents, char letter, uint32_t next)
{
    char before = contents->previous;
    contents->previous = letter;
    if (letter == '\n' && before == '\r') {
        return;
    }
    if (letter == '\n' || letter == '\r') {
        if (contents->equal && contents->track[contents->matched] == '\n') {
            contents->matched++;
            return;
        }
        contents->found |= contents->equal && contents->track[contents->matched] == '\0';
        contents->equal = true;
        contents->matched = 0;
        return;
    }
    contents->equal = contents->equal && contents->track[contents->matched] == letter;
    contents->matched += contents->equal ? 1u : 0u;
    contents->end = letter > ' ' ? next : contents->end;
}

static bool scan(HalFile file, const char *track, Contents *contents)
{
    char chunk[CHUNK_BYTES];
    *contents = (Contents){.track = track, .equal = true};
    for (uint32_t offset = 0; offset < file.size; offset += CHUNK_BYTES) {
        uint32_t length = file.size - offset < CHUNK_BYTES ? file.size - offset : CHUNK_BYTES;
        if (!hal_file_read(file, offset, chunk, length)) {
            return false;
        }
        for (uint32_t index = 0; index < length; index++) {
            feed(contents, chunk[index], offset + index + 1);
        }
    }
    return true;
}

static bool create(void)
{
    char blanks[CHUNK_BYTES];
    for (uint32_t index = 0; index < CHUNK_BYTES; index++) {
        blanks[index] = ' ';
    }
    if (hal_file_reserve(FAVORITE_PLAYLIST_PATH, PLAYLIST_BYTES) != HAL_RESERVE_CREATED) {
        return false;
    }
    for (uint32_t offset = 0; offset < PLAYLIST_BYTES; offset += CHUNK_BYTES) {
        if (!hal_file_write_at(FAVORITE_PLAYLIST_PATH, offset, blanks, CHUNK_BYTES)) {
            return false;
        }
    }
    return true;
}

static bool is_part(const LibraryTrack *record)
{
    return record->start_ms != 0 || record->end_ms != 0;
}

static bool compose(Block *block, const Library *library, const LibraryTrack *record)
{
    char path[PLAYER_PATH_CAPACITY];
    StringBuilder *builder = &block->builder;
    *builder = string_builder_make(block->text + HEADER_BYTES, sizeof block->text - HEADER_BYTES);
    if (!library_read_string(library, record->path, path, sizeof path)) {
        return false;
    }
    if (is_part(record)) {
        string_builder_append(builder, PLAYLIST_START_DIRECTIVE);
        string_builder_append_unsigned(builder, record->start_ms);
        string_builder_append_char(builder, '\n');
    }
    string_builder_append(builder, path);
    return !builder->overflowed;
}

static FavoritePlaylistResult append(HalFile file, Block *block)
{
    Contents contents;
    if (!scan(file, block->text + HEADER_BYTES, &contents)) {
        return FAVORITE_PLAYLIST_FAILED;
    }
    if (contents.found) {
        return FAVORITE_PLAYLIST_PRESENT;
    }
    string_builder_append_char(&block->builder, '\n');
    const char *head = contents.end == 0 ? "#EXTM3U\n" : "\n";
    uint32_t head_length = contents.end == 0 ? HEADER_BYTES : 1;
    char *start = block->text + HEADER_BYTES - head_length;
    for (uint32_t index = 0; index < head_length; index++) {
        start[index] = head[index];
    }
    uint32_t length = head_length + (uint32_t)block->builder.length;
    return !block->builder.overflowed && contents.end + length <= file.size &&
                   hal_file_write_at(FAVORITE_PLAYLIST_PATH, contents.end, start, length)
               ? FAVORITE_PLAYLIST_ADDED
               : FAVORITE_PLAYLIST_FAILED;
}

static bool open_playlist(HalFile *file)
{
    return hal_file_open(file, FAVORITE_PLAYLIST_PATH) ||
           (create() && hal_file_open(file, FAVORITE_PLAYLIST_PATH));
}

FavoritePlaylistResult favorite_playlist_add(const Library *library, uint16_t track)
{
    LibraryTrack record;
    Block block;
    HalFile file;
    if (!library_read_track(library, track, &record) || !compose(&block, library, &record) ||
        !open_playlist(&file)) {
        return FAVORITE_PLAYLIST_FAILED;
    }
    FavoritePlaylistResult result = append(file, &block);
    hal_file_close(&file);
    if (result != FAVORITE_PLAYLIST_FAILED) {
        remember(library, track);
    }
    return result;
}

const char *favorite_playlist_message(FavoritePlaylistResult result)
{
    static const StringId MESSAGES[] = {STR_FAVORITE_ADDED, STR_FAVORITE_ALREADY,
                                        STR_FAVORITE_FAILED};
    return i18n_text(MESSAGES[result]);
}
