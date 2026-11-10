#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library/byte_source.h"

enum {
    LYRICS_LINE_CAPACITY = 192,
    LYRICS_FILE_BYTES = 65536
};

typedef struct LyricsLine {
    uint32_t time_ms;
    uint32_t offset;
    uint16_t bytes;
} LyricsLine;

typedef struct LyricsIndex {
    LyricsLine lines[LYRICS_LINE_CAPACITY];
    uint16_t count;
    bool truncated;
} LyricsIndex;

bool lyrics_parse(LyricsIndex *index, ByteSource source, uint32_t bytes);
int lyrics_current(const LyricsIndex *index, uint32_t position_ms);
bool lyrics_text(ByteSource source, const LyricsLine *line, char *text, size_t capacity);
