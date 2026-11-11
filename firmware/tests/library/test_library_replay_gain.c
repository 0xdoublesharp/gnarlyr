#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "library/library_builder.h"
#include "library/library_gain.h"

enum {
    CAPACITY = 32768,
    HEADER_SIZE = 20,
    RECORD_SIZE = 20,
    BUILDER_MEMORY = 360 * 1024,
    SCRATCH_SIZE = 32 * 1024 * 1024
};

typedef struct Image {
    uint8_t bytes[CAPACITY];
    uint32_t size;
    uint32_t fail_read;
    uint32_t fail_write;
    uint32_t bytes_read;
} Image;

static Image image;
static Library library;
static uint32_t base_size;

static uint32_t digest(uint32_t hash, const uint8_t *bytes, uint32_t size)
{
    for (uint32_t at = 0; at < size; at++) {
        hash = (hash ^ bytes[at]) * UINT32_C(16777619);
    }
    return hash;
}

static void put32(uint8_t *bytes, uint32_t value)
{
    for (uint32_t at = 0; at < 4; at++) {
        bytes[at] = (uint8_t)(value >> (at * 8));
    }
}

static bool image_read(void *context, uint32_t offset, void *out, uint32_t length)
{
    Image *input = context;
    input->bytes_read += length;
    if (offset > input->size || length > input->size - offset ||
        (offset <= input->fail_read && input->fail_read - offset < length)) {
        return false;
    }
    memcpy(out, input->bytes + offset, length);
    return true;
}

static bool image_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Image *output = context;
    if (offset > CAPACITY || length > CAPACITY - offset ||
        (offset <= output->fail_write && output->fail_write - offset < length)) {
        return false;
    }
    memcpy(output->bytes + offset, bytes, length);
    if (offset + length > output->size) {
        output->size = offset + length;
    }
    return true;
}

static void open_image(void)
{
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, (ByteSource){image_read, &image}, image.size));
    image.bytes_read = 0;
}

static void base_image(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    image = (Image){.size = size, .fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    memcpy(image.bytes, bytes, size);
    base_size = size;
    open_image();
}

static ReplayGain expected_gain(uint32_t index)
{
    return (ReplayGain){100000 + index, 80000 + index, 50000 + index, 40000 + index};
}

static void record_checksum(uint32_t index)
{
    uint8_t prefix[8];
    put32(prefix, library_fingerprint(&library));
    put32(prefix + 4, index);
    uint8_t *record = image.bytes + base_size + HEADER_SIZE + index * RECORD_SIZE;
    uint32_t hash = digest(digest(UINT32_C(2166136261), prefix, sizeof prefix), record, 16);
    put32(record + 16, hash);
}

static void append_gains(void)
{
    uint32_t count = library_count(&library, LIBRARY_TRACKS);
    uint8_t *header = image.bytes + base_size;
    memcpy(header, "DSRG", 4);
    header[4] = 1;
    header[6] = RECORD_SIZE;
    put32(header + 8, count);
    put32(header + 12, library_fingerprint(&library));
    put32(header + 16, digest(UINT32_C(2166136261), header, 16));
    for (uint32_t index = 0; index < count; index++) {
        ReplayGain gain = expected_gain(index);
        uint8_t *record = header + HEADER_SIZE + index * RECORD_SIZE;
        put32(record, gain.track);
        put32(record + 4, gain.album);
        put32(record + 8, gain.track_safe);
        put32(record + 12, gain.album_safe);
        record_checksum(index);
    }
    image.size = base_size + HEADER_SIZE + count * RECORD_SIZE;
}

static void check_gain(uint32_t index, ReplayGain expected)
{
    ReplayGain actual = {1, 2, 3, 4};
    CHECK(library_read_replay_gain(&library, index, &actual));
    CHECK_EQUAL(expected.track, actual.track);
    CHECK_EQUAL(expected.album, actual.album);
    CHECK_EQUAL(expected.track_safe, actual.track_safe);
    CHECK_EQUAL(expected.album_safe, actual.album_safe);
}

static void legacy_and_reserved_zero_tails_have_missing_gain(void)
{
    base_image();
    check_gain(0, (ReplayGain){0});
    CHECK_EQUAL(0, image.bytes_read);
    image.size += 4096;
    open_image();
    check_gain(0, (ReplayGain){0});
    CHECK(image.bytes_read <= HEADER_SIZE);
    ReplayGain gain = {1, 2, 3, 4};
    CHECK(!library_read_replay_gain(&library, FIXTURE_TRACKS, &gain));
    CHECK_EQUAL(0, gain.track);
}

static void optional_gains_preserve_the_library_fingerprint_and_use_bounded_reads(void)
{
    base_image();
    uint32_t fingerprint = library_fingerprint(&library);
    append_gains();
    open_image();
    CHECK_EQUAL(fingerprint, library_fingerprint(&library));
    for (uint32_t index = 0; index < FIXTURE_TRACKS; index++) {
        image.bytes_read = 0;
        check_gain(index, expected_gain(index));
        CHECK(image.bytes_read <= 2 * HEADER_SIZE + RECORD_SIZE);
    }
}

static void corrupt_headers_and_truncated_extensions_are_rejected_without_gains(void)
{
    for (uint32_t offset = 4; offset < HEADER_SIZE; offset++) {
        base_image();
        append_gains();
        image.bytes[base_size + offset] ^= 1;
        if (offset < 16) {
            put32(image.bytes + base_size + 16,
                  digest(UINT32_C(2166136261), image.bytes + base_size, 16));
        }
        open_image();
        ReplayGain gain = {1, 2, 3, 4};
        CHECK(!library_read_replay_gain(&library, 0, &gain));
        CHECK_EQUAL(0, gain.track);
    }
    for (uint32_t size = 4; size < HEADER_SIZE + FIXTURE_TRACKS * RECORD_SIZE; size++) {
        base_image();
        append_gains();
        image.size = base_size + size;
        open_image();
        ReplayGain gain = {1, 2, 3, 4};
        CHECK(!library_read_replay_gain(&library, 0, &gain));
        CHECK_EQUAL(0, gain.album);
    }
}

static void records_are_bound_to_the_library_and_track_and_fail_closed(void)
{
    for (uint32_t offset = 0; offset < RECORD_SIZE; offset++) {
        base_image();
        append_gains();
        image.bytes[base_size + HEADER_SIZE + offset] ^= 1;
        open_image();
        ReplayGain gain = {1, 2, 3, 4};
        CHECK(!library_read_replay_gain(&library, 0, &gain));
        CHECK_EQUAL(0, gain.track_safe);
        check_gain(1, expected_gain(1));
    }
    base_image();
    append_gains();
    memcpy(image.bytes + base_size + HEADER_SIZE,
           image.bytes + base_size + HEADER_SIZE + RECORD_SIZE, RECORD_SIZE);
    open_image();
    ReplayGain gain;
    CHECK(!library_read_replay_gain(&library, 0, &gain));
    base_image();
    append_gains();
    image.bytes[LIBRARY_HEADER_SIZE + 16] ^= 1;
    open_image();
    CHECK(!library_read_replay_gain(&library, 0, &gain));
}

static void read_failures_and_invalid_safe_amplitudes_do_not_escape(void)
{
    base_image();
    append_gains();
    open_image();
    image.fail_read = base_size;
    ReplayGain gain = {1, 2, 3, 4};
    CHECK(!library_read_replay_gain(&library, 0, &gain));
    CHECK_EQUAL(0, gain.track);
    image.fail_read = base_size + HEADER_SIZE;
    CHECK(!library_read_replay_gain(&library, 0, &gain));
    CHECK_EQUAL(0, gain.album);
    image.fail_read = UINT32_MAX;
    put32(image.bytes + base_size + HEADER_SIZE + 8, UINT32_MAX);
    record_checksum(0);
    CHECK(!library_read_replay_gain(&library, 0, &gain));
    CHECK_EQUAL(0, gain.track_safe);
    put32(image.bytes + base_size + HEADER_SIZE + 8, 0);
    record_checksum(0);
    CHECK(!library_read_replay_gain(&library, 0, &gain));
    CHECK_EQUAL(0, gain.album_safe);
}

static bool scratch_read(void *context, uint32_t offset, void *out, uint32_t length)
{
    if (offset > SCRATCH_SIZE || length > SCRATCH_SIZE - offset) {
        return false;
    }
    memcpy(out, (uint8_t *)context + offset, length);
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    if (offset > SCRATCH_SIZE || length > SCRATCH_SIZE - offset) {
        return false;
    }
    memcpy((uint8_t *)context + offset, bytes, length);
    return true;
}

static BuilderResult build(bool tagged, uint32_t capacity, uint32_t *length)
{
    static uint8_t *memory;
    static uint8_t *scratch;
    if (memory == NULL) {
        memory = calloc(BUILDER_MEMORY, 1);
        scratch = calloc(SCRATCH_SIZE, 1);
    }
    CHECK(memory != NULL && scratch != NULL);
    LibraryBuilder builder;
    library_builder_init(&builder, memory, BUILDER_MEMORY,
                         (BuilderScratch){scratch_read, scratch_write, scratch, SCRATCH_SIZE});
    for (uint32_t index = 2; index > 0; index--) {
        BuilderTrack track = {.path = index == 1 ? "a.flac" : "b.flac",
                              .title = index == 1 ? "A" : "B",
                              .artist = "Artist",
                              .album_artist = "Artist",
                              .album = "Album",
                              .genre = "Genre",
                              .disc = 1,
                              .number = (uint8_t)index,
                              .duration_ms = 60000,
                              .replay_gain = tagged ? expected_gain(index) : (ReplayGain){0}};
        CHECK_EQUAL(BUILDER_OK, library_builder_add_track(&builder, &track));
    }
    return library_builder_finish(&builder, (ByteSink){image_write, &image}, capacity, length);
}

static void builder_keeps_gain_with_sorted_tracks_and_omits_an_empty_extension(void)
{
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    uint32_t length = 0;
    CHECK_EQUAL(BUILDER_OK, build(false, 0, &length));
    uint32_t plain_size = length;
    uint8_t plain[CAPACITY];
    memcpy(plain, image.bytes, length);
    open_image();
    uint32_t fingerprint = library_fingerprint(&library);
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    CHECK_EQUAL(BUILDER_OK, build(true, CAPACITY, &length));
    CHECK_EQUAL(plain_size + HEADER_SIZE + 2 * RECORD_SIZE, length);
    CHECK(memcmp(plain, image.bytes, plain_size) == 0);
    open_image();
    CHECK_EQUAL(fingerprint, library_fingerprint(&library));
    check_gain(0, expected_gain(1));
    check_gain(1, expected_gain(2));
}

static void insufficient_extension_capacity_and_failed_writes_do_not_publish(void)
{
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    uint32_t length = 0;
    CHECK_EQUAL(BUILDER_OK, build(false, 0, &length));
    uint32_t plain_size = length;
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    CHECK_EQUAL(BUILDER_NO_ROOM, build(true, plain_size + HEADER_SIZE + RECORD_SIZE, &length));
    CHECK_EQUAL(0, image.size);
    image = (Image){.fail_read = UINT32_MAX, .fail_write = plain_size};
    CHECK_EQUAL(BUILDER_WRITE_FAILED, build(true, CAPACITY, &length));
    CHECK(memcmp(image.bytes, "DSDB", 4) != 0);
}

static void removing_gain_from_reserved_storage_erases_the_previous_extension(void)
{
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    uint32_t length = 0;
    CHECK_EQUAL(BUILDER_OK, build(true, CAPACITY, &length));
    open_image();
    uint32_t fingerprint = library_fingerprint(&library);
    check_gain(0, expected_gain(1));
    CHECK_EQUAL(BUILDER_OK, build(false, CAPACITY, &length));
    open_image();
    CHECK_EQUAL(fingerprint, library_fingerprint(&library));
    CHECK(image.size > length);
    check_gain(0, (ReplayGain){0});
    check_gain(1, (ReplayGain){0});
}

static int write_library(const char *path)
{
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    uint32_t length = 0;
    if (build(true, CAPACITY, &length) != BUILDER_OK) {
        return 2;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return 3;
    }
    bool written = fwrite(image.bytes, 1, length, file) == length;
    return fclose(file) == 0 && written ? 0 : 3;
}

static int read_library(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 3;
    }
    image = (Image){.fail_read = UINT32_MAX, .fail_write = UINT32_MAX};
    image.size = (uint32_t)fread(image.bytes, 1, CAPACITY, file);
    (void)fclose(file);
    if (library_open(&library, (ByteSource){image_read, &image}, image.size) != LIBRARY_OK) {
        return 4;
    }
    for (uint32_t index = 0; index < library_count(&library, LIBRARY_TRACKS); index++) {
        ReplayGain gain;
        if (!library_read_replay_gain(&library, index, &gain)) {
            return 5;
        }
        printf("%u %u %u %u\n", (unsigned)gain.track, (unsigned)gain.album,
               (unsigned)gain.track_safe, (unsigned)gain.album_safe);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--write") == 0) {
        return write_library(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--read") == 0) {
        return read_library(argv[2]);
    }
    if (argc != 1) {
        return 2;
    }
    static const TestCase cases[] = {
        TEST_CASE(legacy_and_reserved_zero_tails_have_missing_gain),
        TEST_CASE(optional_gains_preserve_the_library_fingerprint_and_use_bounded_reads),
        TEST_CASE(corrupt_headers_and_truncated_extensions_are_rejected_without_gains),
        TEST_CASE(records_are_bound_to_the_library_and_track_and_fail_closed),
        TEST_CASE(read_failures_and_invalid_safe_amplitudes_do_not_escape),
        TEST_CASE(builder_keeps_gain_with_sorted_tracks_and_omits_an_empty_extension),
        TEST_CASE(insufficient_extension_capacity_and_failed_writes_do_not_publish),
        TEST_CASE(removing_gain_from_reserved_storage_erases_the_previous_extension),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
