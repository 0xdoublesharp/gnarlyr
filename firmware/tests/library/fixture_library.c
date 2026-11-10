#include "fixture_library.h"

#include "library/library_content.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fixture_data[FIXTURE_CAPACITY];
static uint32_t fixture_size;
static uint8_t covers_data[FIXTURE_COVERS_CAPACITY];
static uint32_t covers_size;

static uint32_t load_fixture(const char *path, uint8_t *buffer, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "missing fixture %s\n", path);
        exit(1);
    }
    uint32_t size = (uint32_t)fread(buffer, 1, capacity, file);
    fclose(file);
    return size;
}

const uint8_t *fixture_bytes(uint32_t *size)
{
    if (fixture_size == 0) {
        fixture_size =
            load_fixture(ECHO_FIXTURE_DIR "/small_library.db", fixture_data, sizeof fixture_data);
    }
    *size = fixture_size;
    return fixture_data;
}

const uint8_t *fixture_covers_bytes(uint32_t *size)
{
    if (covers_size == 0) {
        covers_size =
            load_fixture(ECHO_FIXTURE_DIR "/small_covers.bin", covers_data, sizeof covers_data);
    }
    *size = covers_size;
    return covers_data;
}

static Library *open_bytes(FixtureLibrary *fixture, const uint8_t *bytes, uint32_t size)
{
    memcpy(fixture->bytes, bytes, size);
    fixture->size = size;
    fixture->memory = (MemorySource){.bytes = fixture->bytes, .size = size};
    library_open(&fixture->library, memory_source(&fixture->memory), size);
    return &fixture->library;
}

Library *fixture_library_open(FixtureLibrary *fixture)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    return open_bytes(fixture, bytes, size);
}

Library *fixture_cue_library_open(FixtureLibrary *fixture)
{
    static uint8_t cue_data[FIXTURE_CAPACITY];
    uint32_t size = load_fixture(ECHO_FIXTURE_DIR "/cue_music.db", cue_data, sizeof cue_data);
    return open_bytes(fixture, cue_data, size);
}

bool fixture_library_identify(Library *library)
{
    LibraryContent job;
    library_content_begin(&job, library);
    while (library_content_step(&job) == LIBRARY_CONTENT_WORKING) {
    }
    return job.state == LIBRARY_CONTENT_READY;
}
