#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tags_internal.h"

enum {
    MP4_CHAPTER_MAX = 255,
    MP4_CHAPTER_TITLE = 64
};

typedef struct Mp4Chapters {
    TagReader reader;
    uint32_t count;
    bool prefixed;
    uint32_t start_ms[MP4_CHAPTER_MAX];
    uint32_t offset[MP4_CHAPTER_MAX];
    uint16_t length[MP4_CHAPTER_MAX];
} Mp4Chapters;

bool mp4_chapters_open(Mp4Chapters *chapters, ByteSource source, uint32_t size,
                       uint32_t duration_ms);
void mp4_chapter_title(const Mp4Chapters *chapters, uint32_t index, char *title);
