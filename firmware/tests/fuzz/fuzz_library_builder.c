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
    TEXT_BYTES = 64,
    CONTROL_BYTES = 8
};

typedef struct MemoryFile {
    uint8_t *bytes;
    uint32_t size;
    uint32_t used;
    uint32_t reads;
    uint32_t writes;
    uint32_t fail_read;
    uint32_t fail_write;
} MemoryFile;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    MemoryFile *file = context;
    file->reads++;
    if (file->fail_read != 0 && file->reads == file->fail_read) {
        return false;
    }
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(bytes, file->bytes + offset, length);
    return true;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    MemoryFile *file = context;
    file->writes++;
    if (file->fail_write != 0 && file->writes == file->fail_write) {
        return false;
    }
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
    switch ((bytes[0] >> 4) & 3u) {
    case 0:
        snprintf(out, TEXT_BYTES, "%c/%02x/%02u-%02x.flac", 'A' + bytes[0] % 26, bytes[1], index,
                 bytes[2]);
        break;
    case 1:
        snprintf(out, TEXT_BYTES, "Audiobooks/%02x/%02u.m4b", bytes[1], index);
        break;
    case 2:
        snprintf(out, TEXT_BYTES, "Shared/%02x.flac", bytes[2]);
        break;
    default:
        snprintf(out, TEXT_BYTES, "/%02x//%02u.flac", bytes[1], index);
        break;
    }
}

static uint32_t capacity(uint32_t maximum, uint8_t value)
{
    if (value == 0) {
        return maximum;
    }
    return (uint32_t)((uint64_t)maximum * (value - 1u) / (UINT8_MAX - 1u));
}

static uint32_t fault_ordinal(uint8_t value, uint8_t page)
{
    return value == 0 ? 0 : value + (uint32_t)(page & 15u) * UINT8_MAX;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t player[PLAYER_BYTES];
    static uint8_t scratch_bytes[SCRATCH_BYTES];
    static uint8_t output_bytes[OUTPUT_BYTES];
    memset(player, 0, sizeof player);
    memset(scratch_bytes, 0, sizeof scratch_bytes);
    memset(output_bytes, 0, sizeof output_bytes);
    uint8_t controls[CONTROL_BYTES] = {0};
    size_t control_size = size < sizeof controls ? size : sizeof controls;
    memcpy(controls, data, control_size);
    uint32_t player_size = capacity(sizeof player, controls[0]);
    uint32_t scratch_size = capacity(sizeof scratch_bytes, controls[1]);
    uint32_t output_size = capacity(sizeof output_bytes, controls[2]);
    MemoryFile scratch = {.bytes = scratch_bytes,
                          .size = scratch_size,
                          .fail_read = fault_ordinal(controls[3], controls[7]),
                          .fail_write = fault_ordinal(controls[4], controls[7])};
    MemoryFile output = {.bytes = output_bytes,
                         .size = output_size,
                         .fail_write = fault_ordinal(controls[5], controls[7])};
    LibraryBuilder builder;
    library_builder_init(&builder, player, player_size,
                         (BuilderScratch){read_file, write_file, &scratch, scratch_size});
    const uint8_t *track_data = size > CONTROL_BYTES ? data + CONTROL_BYTES : data + size;
    size_t track_size = size > CONTROL_BYTES ? size - CONTROL_BYTES : 0;
    uint32_t tracks = (uint32_t)(track_size / TRACK_BYTES);
    if (tracks > TRACK_LIMIT) {
        tracks = TRACK_LIMIT;
    }
    uint32_t added = 0;
    for (uint32_t index = 0; index < tracks; index++) {
        const uint8_t *bytes = track_data + index * TRACK_BYTES;
        char file[TEXT_BYTES];
        char title[TEXT_BYTES];
        char artist[TEXT_BYTES];
        char album_artist[TEXT_BYTES];
        char album[TEXT_BYTES];
        char genre[TEXT_BYTES];
        char sheet[TEXT_BYTES];
        path(file, bytes, index);
        text(title, "Title", bytes[2], bytes[3]);
        text(artist, "Artist", bytes[3] % 9, bytes[4] % 7);
        text(album_artist, "Album Artist", bytes[4] % 6, bytes[5] % 5);
        text(album, "Album", bytes[5] % 11, bytes[6] % 13);
        text(genre, "Genre", bytes[6] % 8, bytes[7] % 3);
        text(sheet, "Sheet", bytes[1] % 4, bytes[2] % 3);
        const char *empty = "";
        BuilderTrack track = {
            .path = file,
            .title = (bytes[0] & 8u) != 0 ? empty : title,
            .artist = (bytes[1] & 8u) != 0 ? empty : artist,
            .album_artist = (bytes[0] & 1u) != 0 ? album_artist : artist,
            .album = (bytes[2] & 8u) != 0 ? empty : album,
            .genre = (bytes[3] & 8u) != 0 ? empty : genre,
            .year = (bytes[4] & 8u) != 0 ? 0 : (uint16_t)(1900 + bytes[1]),
            .disc = (bytes[5] & 8u) != 0 ? 0 : (uint8_t)(1 + bytes[2] % 4),
            .number = (bytes[6] & 8u) != 0 ? 0 : (uint8_t)(1 + bytes[3] % 40),
            .duration_ms = (uint32_t)bytes[4] * 1000u,
            .album_artist_guessed = (bytes[0] & 2u) != 0,
            .start_ms = (uint32_t)bytes[5] * 100u,
            .end_ms = (bytes[7] & 8u) != 0 ? 0 : (uint32_t)(bytes[5] + bytes[6] + 1u) * 100u,
            .sheet = (bytes[1] & 3u) == 0   ? NULL
                     : (bytes[1] & 3u) == 1 ? empty
                                            : sheet,
            .chapters = (bytes[0] & 4u) != 0,
            .replay_gain = {
                .track = (bytes[6] & 1u) != 0 ? REPLAY_GAIN_UNITY + bytes[6] : 0,
                .album = (bytes[7] & 1u) != 0 ? REPLAY_GAIN_UNITY + bytes[7] : 0,
            }};
        if ((bytes[7] & 16u) != 0) {
            track.album_artist = empty;
        }
        if ((bytes[7] & 32u) != 0) {
            track.path = empty;
        }
        BuilderResult result = library_builder_add_track(&builder, &track);
        fuzz_require(result >= BUILDER_OK && result <= BUILDER_NO_ROOM);
        if (result == BUILDER_OK) {
            added++;
        } else {
            break;
        }
    }
    if (track_size > 1 && (controls[6] & 8u) != 0) {
        BuilderResult begin = library_builder_begin_playlist(&builder, "Fuzz Mix", "Playlists");
        fuzz_require(begin >= BUILDER_OK && begin <= BUILDER_NO_ROOM);
        for (uint32_t index = 0; begin == BUILDER_OK && index < tracks; index += 2) {
            char entry[TEXT_BYTES];
            path(entry, track_data + index * TRACK_BYTES, index);
            BuilderResult result = library_builder_playlist_entry(
                &builder, entry,
                (track_data[index * TRACK_BYTES] & 16u) != 0 ? BUILDER_ANY_START : 0);
            fuzz_require(result >= BUILDER_OK && result <= BUILDER_NO_ROOM);
        }
    }
    uint32_t length = 0;
    BuilderResult result =
        library_builder_finish(&builder, (ByteSink){write_file, &output}, output_size, &length);
    fuzz_require(result >= BUILDER_OK && result <= BUILDER_NO_ROOM);
    if (result == BUILDER_OK) {
        fuzz_require(length == output.used && length <= output_size);
        MemorySource source = {.bytes = output.bytes, .size = length};
        Library library;
        fuzz_require(library_open(&library, memory_source(&source), length) == LIBRARY_OK);
        fuzz_require(library_count(&library, LIBRARY_TRACKS) == added);
    } else {
        fuzz_require(length == 0);
    }
    return 0;
}
