#include "spoken_library.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "library/library_builder.h"

enum {
    BUILDER_MEMORY = 360 * 1024,
    SCRATCH_SIZE = 32 * 1024 * 1024,
    OUTPUT_CAPACITY = 256 * 1024,
    PATH_CAPACITY = 260
};

const char SPOKEN_CHAPTER_ONE_PATH[] = "Audiobooks/Book One/01 - Arrival.mp3";
const char SPOKEN_CHAPTER_TWO_PATH[] = "Audiobooks/Book One/02 - The Road.mp3";
const char SPOKEN_CHAPTER_THREE_PATH[] = "Audiobooks/Book One/03 - Home.mp3";
const char SPOKEN_PARTS_PATH[] = "Audiobooks/Zebra Tale.m4b";
const char SPOKEN_NESTED_PARTS_PATH[] = "Audiobooks/Writer/Title/Zebra Tale.m4b";
const char SPOKEN_NESTED_CHAPTER_TWO_PATH[] = "Audiobooks/Author/Book One/02 - The Road.mp3";
const char SPOKEN_SINGLE_BOOK_PATH[] = "Audiobooks/Single Book.mp3";
const char SPOKEN_SONG_PATH[] = "Music/Band/Record/01 - Song.flac";
const char SPOKEN_EPISODE_A_PATH[] = "Podcasts/Show/Episode A.mp3";
const char SPOKEN_EPISODE_B_PATH[] = "Podcasts/Show/Episode B.mp3";

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t size;
    uint32_t length;
} Buffer;

static bool buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(bytes, buffer->bytes + offset, length);
    return true;
}

static bool buffer_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(buffer->bytes + offset, bytes, length);
    buffer->length = offset + length > buffer->length ? offset + length : buffer->length;
    return true;
}

static void fail(const char *what)
{
    fprintf(stderr, "spoken library: %s\n", what);
    exit(1);
}

static const BuilderTrack TRACKS[] = {
    {SPOKEN_PARTS_PATH, "Dawn", "Reader", "Writer", "Zebra Tale", "Audiobook", 2018, 1, 1,
     SPOKEN_PART_ONE_MS, false, 0, SPOKEN_PART_ONE_MS, "Zebra Tale", true},
    {SPOKEN_PARTS_PATH, "Noon", "Reader", "Writer", "Zebra Tale", "Audiobook", 2018, 1, 2,
     SPOKEN_PART_TWO_MS, false, SPOKEN_PART_ONE_MS, SPOKEN_PART_ONE_MS + SPOKEN_PART_TWO_MS,
     "Zebra Tale", true},
    {SPOKEN_PARTS_PATH, "Dusk", "Reader", "Writer", "Zebra Tale", "Audiobook", 2018, 1, 3,
     SPOKEN_PART_ONE_MS, false, SPOKEN_PART_ONE_MS + SPOKEN_PART_TWO_MS, 0, "Zebra Tale", true},
    {SPOKEN_CHAPTER_ONE_PATH, "Arrival", "Reader", "Author", "Book One", "Audiobook", 2020, 1, 1,
     SPOKEN_CHAPTER_MS, false, 0, 0, NULL, false},
    {SPOKEN_CHAPTER_TWO_PATH, "The Road", "Reader", "Author", "Book One", "Audiobook", 2020, 1, 2,
     SPOKEN_CHAPTER_MS, false, 0, 0, NULL, false},
    {SPOKEN_CHAPTER_THREE_PATH, "Home", "Reader", "Author", "Book One", "Audiobook", 2020, 1, 3,
     SPOKEN_CHAPTER_MS, false, 0, 0, NULL, false},
    {SPOKEN_SINGLE_BOOK_PATH, "Single Book", "Reader", "Writer", "Single Book", "Audiobook", 2019,
     1, 0, SPOKEN_CHAPTER_MS, false, 0, 0, NULL, false},
    {SPOKEN_SONG_PATH, "Song", "Band", "Band", "Record", "Rock", 2001, 1, 1, SPOKEN_SONG_MS, false,
     0, 0, NULL, false},
    {SPOKEN_EPISODE_A_PATH, "Episode A", "Host", "Host", "Show", "Podcast", 2024, 1, 0,
     SPOKEN_EPISODE_MS, false, 0, 0, NULL, false},
    {SPOKEN_EPISODE_B_PATH, "Episode B", "Host", "Host", "Show", "Podcast", 2024, 1, 0,
     SPOKEN_EPISODE_MS, false, 0, 0, NULL, false},
};

static void check_step(BuilderResult result, const char *step)
{
    if (result != BUILDER_OK) {
        fprintf(stderr, "spoken library: %s gave %d\n", step, (int)result);
        exit(1);
    }
}

static const char *nested_path(const char *path)
{
    static const char *const ORIGINAL[] = {SPOKEN_PARTS_PATH, SPOKEN_CHAPTER_ONE_PATH,
                                           SPOKEN_CHAPTER_TWO_PATH, SPOKEN_CHAPTER_THREE_PATH};
    static const char *const NESTED[] = {
        SPOKEN_NESTED_PARTS_PATH, "Audiobooks/Author/Book One/01 - Arrival.mp3",
        SPOKEN_NESTED_CHAPTER_TWO_PATH, "Audiobooks/Author/Book One/03 - Home.mp3"};
    for (size_t index = 0; index < sizeof ORIGINAL / sizeof ORIGINAL[0]; index++) {
        if (path == ORIGINAL[index]) {
            return NESTED[index];
        }
    }
    return path;
}

static void add_tracks(LibraryBuilder *builder, int layout)
{
    for (size_t index = 0; index < sizeof TRACKS / sizeof TRACKS[0]; index++) {
        BuilderTrack track = TRACKS[index];
        track.path = layout == 1 ? nested_path(track.path) : track.path;
        if (layout >= 2 && track.path == SPOKEN_PARTS_PATH) {
            track.path = "Downloads/Writer/Zebra Tale.m4b";
        }
        if (layout >= 2 && track.path == SPOKEN_SINGLE_BOOK_PATH) {
            track.path = "Other/Writer/Single Book.m4b";
        }
        if (layout == 3 && strncmp(track.path, "Audiobooks/", 11) == 0) {
            track.path += 11;
        }
        check_step(library_builder_add_track(builder, &track), track.path);
    }
}

static Library *open_library(SpokenLibrary *spoken, int layout)
{
    uint8_t *memory = malloc(BUILDER_MEMORY);
    Buffer scratch = {.bytes = malloc(SCRATCH_SIZE), .size = SCRATCH_SIZE, .length = 0};
    Buffer output = {.bytes = malloc(OUTPUT_CAPACITY), .size = OUTPUT_CAPACITY, .length = 0};
    if (memory == NULL || scratch.bytes == NULL || output.bytes == NULL) {
        fail("out of host memory");
    }
    LibraryBuilder builder;
    library_builder_init(&builder, memory, BUILDER_MEMORY,
                         (BuilderScratch){buffer_read, buffer_write, &scratch, SCRATCH_SIZE});
    add_tracks(&builder, layout);
    uint32_t length = 0;
    check_step(library_builder_prepare(&builder), "prepare");
    check_step(library_builder_finish(&builder, (ByteSink){buffer_write, &output}, 0, &length),
               "finish");
    free(memory);
    free(scratch.bytes);
    spoken->bytes = output.bytes;
    spoken->size = length;
    spoken->memory = (MemorySource){.bytes = spoken->bytes, .size = spoken->size};
    if (library_open(&spoken->library, memory_source(&spoken->memory), spoken->size) !=
        LIBRARY_OK) {
        fail("open");
    }
    return &spoken->library;
}

Library *spoken_library_open(SpokenLibrary *spoken)
{
    return open_library(spoken, 0);
}

Library *spoken_library_open_nested(SpokenLibrary *spoken)
{
    return open_library(spoken, 1);
}

Library *spoken_library_open_external(SpokenLibrary *spoken)
{
    return open_library(spoken, 2);
}

Library *spoken_library_open_without_shelf(SpokenLibrary *spoken)
{
    return open_library(spoken, 3);
}

void spoken_library_close(SpokenLibrary *spoken)
{
    free(spoken->bytes);
    spoken->bytes = NULL;
}

uint16_t spoken_library_track(const Library *library, const char *path)
{
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    for (uint32_t index = 0; index < count; index++) {
        LibraryTrack track;
        char found[PATH_CAPACITY];
        if (library_read_track(library, index, &track) &&
            library_read_string(library, track.path, found, sizeof found) &&
            strcmp(found, path) == 0) {
            return (uint16_t)index;
        }
    }
    fail("no such track");
    return 0;
}

uint16_t spoken_library_chapter(const Library *library, uint8_t number)
{
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    for (uint32_t index = 0; index < count; index++) {
        LibraryTrack track;
        char found[PATH_CAPACITY];
        if (library_read_track(library, index, &track) && track.number == number &&
            library_read_string(library, track.path, found, sizeof found) &&
            strcmp(found, SPOKEN_PARTS_PATH) == 0) {
            return (uint16_t)index;
        }
    }
    fail("no such chapter");
    return 0;
}
