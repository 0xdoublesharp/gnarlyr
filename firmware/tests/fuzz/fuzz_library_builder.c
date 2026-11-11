#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuzz_source.h"
#include "library/library.h"
#include "library/library_builder.h"
#include "memory_source.h"

enum {
    PLAYER_BYTES = 360 * 1024,
    SCRATCH_BYTES = 32 * 1024 * 1024,
    OUTPUT_BYTES = 2 * 1024 * 1024,
    TRACK_BYTES = 8,
    TRACK_LIMIT = 64,
    TEXT_BYTES = 64
};

typedef struct MemoryFile {
    uint8_t *bytes;
    uint32_t size;
    uint32_t used;
} MemoryFile;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    MemoryFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(bytes, file->bytes + offset, length);
    return true;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    MemoryFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(file->bytes + offset, bytes, length);
    uint32_t end = offset + length;
    file->used = end > file->used ? end : file->used;
    return true;
}

static void text(char *out, const char *prefix, uint8_t first, uint8_t second)
{
    snprintf(out, TEXT_BYTES, "%s-%02x-%02x", prefix, first, second);
}

static void path(char *out, const uint8_t *bytes, uint32_t index)
{
    snprintf(out, TEXT_BYTES, "%c/%02x/%02u-%02x.flac", 'A' + bytes[0] % 26, bytes[1], index,
             bytes[2]);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t player[PLAYER_BYTES];
    static uint8_t scratch_bytes[SCRATCH_BYTES];
    static uint8_t output_bytes[OUTPUT_BYTES];
    memset(player, 0, sizeof player);
    memset(scratch_bytes, 0, sizeof scratch_bytes);
    memset(output_bytes, 0, sizeof output_bytes);
    MemoryFile scratch = {.bytes = scratch_bytes, .size = sizeof scratch_bytes};
    MemoryFile output = {.bytes = output_bytes, .size = sizeof output_bytes};
    LibraryBuilder builder;
    library_builder_init(&builder, player, sizeof player,
                         (BuilderScratch){read_file, write_file, &scratch, scratch.size});
    uint32_t tracks = (uint32_t)(size / TRACK_BYTES);
    if (tracks > TRACK_LIMIT) {
        tracks = TRACK_LIMIT;
    }
    uint32_t added = 0;
    for (uint32_t index = 0; index < tracks; index++) {
        const uint8_t *bytes = data + index * TRACK_BYTES;
        char file[TEXT_BYTES];
        char title[TEXT_BYTES];
        char artist[TEXT_BYTES];
        char album_artist[TEXT_BYTES];
        char album[TEXT_BYTES];
        char genre[TEXT_BYTES];
        path(file, bytes, index);
        text(title, "Title", bytes[2], bytes[3]);
        text(artist, "Artist", bytes[3] % 9, bytes[4] % 7);
        text(album_artist, "Album Artist", bytes[4] % 6, bytes[5] % 5);
        text(album, "Album", bytes[5] % 11, bytes[6] % 13);
        text(genre, "Genre", bytes[6] % 8, bytes[7] % 3);
        BuilderTrack track = {.path = file,
                              .title = title,
                              .artist = artist,
                              .album_artist = (bytes[0] & 1u) != 0 ? album_artist : artist,
                              .album = album,
                              .genre = genre,
                              .year = (uint16_t)(1900 + bytes[1]),
                              .disc = (uint8_t)(1 + bytes[2] % 4),
                              .number = (uint8_t)(1 + bytes[3] % 40),
                              .duration_ms = (uint32_t)bytes[4] * 1000u,
                              .album_artist_guessed = (bytes[0] & 2u) != 0,
                              .start_ms = (uint32_t)bytes[5] * 100u,
                              .end_ms = (uint32_t)(bytes[5] + bytes[6] + 1u) * 100u,
                              .chapters = (bytes[0] & 4u) != 0};
        BuilderResult result = library_builder_add_track(&builder, &track);
        fuzz_require(result >= BUILDER_OK && result <= BUILDER_NO_ROOM);
        if (result == BUILDER_OK) {
            added++;
        } else {
            break;
        }
    }
    if (size > 1 && (data[0] & 8u) != 0) {
        BuilderResult begin = library_builder_begin_playlist(&builder, "Fuzz Mix", "Playlists");
        fuzz_require(begin >= BUILDER_OK && begin <= BUILDER_NO_ROOM);
        for (uint32_t index = 0; begin == BUILDER_OK && index < tracks; index += 2) {
            char entry[TEXT_BYTES];
            path(entry, data + index * TRACK_BYTES, index);
            BuilderResult result = library_builder_playlist_entry(
                &builder, entry, (data[index * TRACK_BYTES] & 16u) != 0 ? BUILDER_ANY_START : 0);
            fuzz_require(result >= BUILDER_OK && result <= BUILDER_NO_ROOM);
        }
    }
    uint32_t length = 0;
    BuilderResult result =
        library_builder_finish(&builder, (ByteSink){write_file, &output}, output.size, &length);
    fuzz_require(result >= BUILDER_OK && result <= BUILDER_NO_ROOM);
    if (result == BUILDER_OK) {
        fuzz_require(length == output.used && length <= output.size);
        MemorySource source = {.bytes = output.bytes, .size = length};
        Library library;
        fuzz_require(library_open(&library, memory_source(&source), length) == LIBRARY_OK);
        fuzz_require(library_count(&library, LIBRARY_TRACKS) == added);
    } else {
        fuzz_require(length == 0);
    }
    return 0;
}
