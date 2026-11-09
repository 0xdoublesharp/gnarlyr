#pragma once

#include <stdint.h>

#include "library/library.h"
#include "memory_source.h"

enum {
    SPOKEN_CHAPTER_MS = 600000,
    SPOKEN_EPISODE_MS = 1200000,
    SPOKEN_SONG_MS = 180000,
    SPOKEN_PART_ONE_MS = 300000,
    SPOKEN_PART_TWO_MS = 600000
};

extern const char SPOKEN_CHAPTER_ONE_PATH[];
extern const char SPOKEN_CHAPTER_TWO_PATH[];
extern const char SPOKEN_CHAPTER_THREE_PATH[];
extern const char SPOKEN_PARTS_PATH[];
extern const char SPOKEN_SINGLE_BOOK_PATH[];
extern const char SPOKEN_SONG_PATH[];
extern const char SPOKEN_EPISODE_A_PATH[];
extern const char SPOKEN_EPISODE_B_PATH[];

typedef struct SpokenLibrary {
    uint8_t *bytes;
    uint32_t size;
    MemorySource memory;
    Library library;
} SpokenLibrary;

Library *spoken_library_open(SpokenLibrary *spoken);
void spoken_library_close(SpokenLibrary *spoken);
uint16_t spoken_library_track(const Library *library, const char *path);
uint16_t spoken_library_chapter(const Library *library, uint8_t number);
