#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/hash.h"
#include "library/byte_source.h"
#include "library/library.h"

enum {
    COVER_FULL_SIZE = 112,
    COVER_HALF_SIZE = 56,
    COVER_FULL_PIXELS = COVER_FULL_SIZE * COVER_FULL_SIZE,
    COVER_HALF_PIXELS = COVER_HALF_SIZE * COVER_HALF_SIZE,
    COVERS_VERSION = 1,
    COVERS_HEADER_SIZE = 16,
    COVERS_ENTRY_SIZE = 8
};

typedef enum CoverSize {
    COVER_FULL,
    COVER_HALF
} CoverSize;

typedef struct Covers {
    ByteSource source;
    uint32_t size;
    uint32_t count;
} Covers;

static inline bool covers_tracks_separate(const char *album_title, uint16_t track_count)
{
    const char *unknown = LIBRARY_UNKNOWN_ALBUM;
    while (*unknown != '\0' && *album_title == *unknown) {
        album_title++;
        unknown++;
    }
    return track_count > 1 && *album_title == *unknown;
}

static inline uint32_t covers_track_key(const char *path)
{
    size_t length = 0;
    while (path[length] != '\0') {
        length++;
    }
    return hash_update(HASH_START * HASH_PRIME, path, length);
}

bool covers_open(Covers *covers, ByteSource source, uint32_t size);
bool covers_find(const Covers *covers, uint32_t key, uint32_t *record);
bool covers_read(const Covers *covers, uint32_t record, CoverSize size, uint16_t *pixels);
