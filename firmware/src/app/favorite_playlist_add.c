#include "favorite_playlist.h"

#include "core/string_builder.h"
#include "hal/storage.h"
#include "library/playlist_reader.h"
#include "player.h"

enum {
    PLAYLIST_BYTES = 32 * 1024,
    CHUNK_BYTES = 256,
    KEY_CAPACITY = PLAYER_PATH_CAPACITY + 32,
    HEADER_BYTES = 8,
    BLOCK_CAPACITY = HEADER_BYTES + KEY_CAPACITY + 2
};

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
        favorite_playlist_remember(library, track);
    }
    return result;
}
