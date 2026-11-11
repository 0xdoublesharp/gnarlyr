#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "library/covers.h"

enum {
    RED_ALBUM = 0,
    BARE_ALBUM = 1,
    BLUE_ALBUM = 2,
    RED = 0xF800,
    BLUE = 0x001F
};

static FixtureLibrary fixture;
static uint8_t bytes[FIXTURE_COVERS_CAPACITY];
static MemorySource memory;
static uint16_t pixels[COVER_FULL_PIXELS];

static uint32_t load_covers(void)
{
    uint32_t size = 0;
    const uint8_t *source = fixture_covers_bytes(&size);
    memcpy(bytes, source, size);
    memory = (MemorySource){.bytes = bytes, .size = size};
    return size;
}

static Covers open_covers(void)
{
    uint32_t size = load_covers();
    Covers covers;
    CHECK(covers_open(&covers, memory_source(&memory), size));
    return covers;
}

static bool all_pixels_are(int count, uint16_t color)
{
    for (int index = 0; index < count; index++) {
        if (pixels[index] != color) {
            return false;
        }
    }
    return true;
}

static uint32_t key_of(uint32_t album)
{
    return library_album_key(fixture_library_open(&fixture), album);
}

static void the_fixture_holds_two_covers(void)
{
    Covers covers = open_covers();
    CHECK_EQUAL(2, covers.count);
}

static void a_cover_is_found_by_album_key_in_both_sizes(void)
{
    Covers covers = open_covers();
    uint32_t record = 0;
    CHECK(covers_find(&covers, key_of(RED_ALBUM), &record));
    CHECK(covers_read(&covers, record, COVER_FULL, pixels));
    CHECK(all_pixels_are(COVER_FULL_PIXELS, RED));
    CHECK(covers_read(&covers, record, COVER_HALF, pixels));
    CHECK(all_pixels_are(COVER_HALF_PIXELS, RED));
    CHECK(covers_find(&covers, key_of(BLUE_ALBUM), &record));
    CHECK(covers_read(&covers, record, COVER_HALF, pixels));
    CHECK(all_pixels_are(COVER_HALF_PIXELS, BLUE));
}

static void an_album_without_art_is_not_found(void)
{
    Covers covers = open_covers();
    uint32_t record = 0;
    CHECK(!covers_find(&covers, key_of(BARE_ALBUM), &record));
}

static void a_file_with_the_wrong_magic_is_rejected(void)
{
    uint32_t size = load_covers();
    bytes[0] = 'X';
    Covers covers;
    CHECK(!covers_open(&covers, memory_source(&memory), size));
}

static void a_file_with_other_cover_sizes_is_rejected(void)
{
    uint32_t size = load_covers();
    bytes[8] = 100;
    Covers covers;
    CHECK(!covers_open(&covers, memory_source(&memory), size));
}

static void a_truncated_file_is_rejected(void)
{
    uint32_t size = load_covers();
    Covers covers;
    CHECK(!covers_open(&covers, memory_source(&memory), size - 1));
    CHECK(!covers_open(&covers, memory_source(&memory), 4));
}

static void a_failing_read_is_reported(void)
{
    Covers covers = open_covers();
    uint32_t record = 0;
    CHECK(covers_find(&covers, key_of(RED_ALBUM), &record));
    memory.failing = true;
    CHECK(!covers_read(&covers, record, COVER_FULL, pixels));
}

enum {
    MANY_COVERS = 2673,
    MANY_HEADER = COVERS_HEADER_SIZE,
    MANY_RECORDS = MANY_HEADER + MANY_COVERS * COVERS_ENTRY_SIZE,
    MANY_SIZE = MANY_RECORDS + (COVER_FULL_PIXELS + COVER_HALF_PIXELS) * 2,
    MANY_CLAIMED = MANY_RECORDS + MANY_COVERS * (COVER_FULL_PIXELS + COVER_HALF_PIXELS) * 2
};

typedef struct CountingSource {
    const uint8_t *bytes;
    uint32_t reads;
} CountingSource;

static bool counted_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    CountingSource *source = context;
    source->reads++;
    memcpy(buffer, source->bytes + offset, length);
    return true;
}

static void put32(uint8_t *at, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        at[index] = (uint8_t)(value >> (8 * index));
    }
}

static uint32_t many_keys[MANY_COVERS];

static int by_value(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return a < b ? -1 : a > b;
}

static void make_keys(void)
{
    uint32_t state = 12345;
    for (uint32_t index = 0; index < MANY_COVERS; index++) {
        state = state * 1664525u + 1013904223u;
        many_keys[index] = state | 1u;
    }
    qsort(many_keys, MANY_COVERS, sizeof many_keys[0], by_value);
}

static uint32_t many_key(uint32_t index)
{
    return many_keys[index];
}

static void a_large_index_finds_each_cover_in_at_most_two_reads(void)
{
    static uint8_t many[MANY_SIZE];
    make_keys();
    memcpy(many, "DSCV", 4);
    many[4] = 1;
    put32(many + 6, MANY_COVERS);
    many[8] = COVER_FULL_SIZE;
    many[10] = COVER_HALF_SIZE;
    for (uint32_t index = 0; index < MANY_COVERS; index++) {
        put32(many + MANY_HEADER + index * COVERS_ENTRY_SIZE, many_key(index));
        put32(many + MANY_HEADER + index * COVERS_ENTRY_SIZE + 4, MANY_RECORDS);
    }
    CountingSource source = {.bytes = many};
    Covers covers;
    CHECK(covers_open(&covers, (ByteSource){counted_read, &source}, MANY_CLAIMED));
    uint32_t worst = 0;
    for (uint32_t index = 0; index < MANY_COVERS; index += 7) {
        uint32_t record = 0;
        source.reads = 0;
        CHECK(covers_find(&covers, many_key(index), &record));
        CHECK_EQUAL(MANY_RECORDS, record);
        worst = source.reads > worst ? source.reads : worst;
        CHECK(!covers_find(&covers, many_key(index) - 1, &record));
    }
    CHECK(worst <= 2);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_fixture_holds_two_covers),
        TEST_CASE(a_cover_is_found_by_album_key_in_both_sizes),
        TEST_CASE(an_album_without_art_is_not_found),
        TEST_CASE(a_file_with_the_wrong_magic_is_rejected),
        TEST_CASE(a_file_with_other_cover_sizes_is_rejected),
        TEST_CASE(a_truncated_file_is_rejected),
        TEST_CASE(a_failing_read_is_reported),
        TEST_CASE(a_large_index_finds_each_cover_in_at_most_two_reads),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
