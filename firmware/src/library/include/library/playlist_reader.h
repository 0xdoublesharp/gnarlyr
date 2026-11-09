#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"

enum {
    PLAYLIST_CHUNK = 512,
    PLAYLIST_ENTRY_CAPACITY = 1024
};

#define PLAYLIST_NO_START UINT32_MAX

typedef struct PlaylistReader {
    ByteSource source;
    uint32_t size;
    uint32_t offset;
    bool utf8;
    uint32_t start_ms;
    uint8_t chunk[PLAYLIST_CHUNK];
    uint32_t chunk_start;
    uint32_t chunk_length;
} PlaylistReader;

bool playlist_reader_open(PlaylistReader *reader, ByteSource source, uint32_t size);
bool playlist_reader_next(PlaylistReader *reader, char *entry, uint32_t capacity);
