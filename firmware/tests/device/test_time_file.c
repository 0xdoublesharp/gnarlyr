#include "check.h"
#include "core/hash.h"
#include "device/time_file.h"

enum {
    NOVEMBER_2026 = 1794482460u
};

static void put32(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static void seal(uint8_t *bytes)
{
    put32(bytes + 28, hash_update(HASH_START, bytes, 28));
}

static void written(uint8_t bytes[TIME_FILE_BYTES], uint32_t utc, int16_t offset)
{
    for (int index = 0; index < TIME_FILE_BYTES; index++) {
        bytes[index] = 0;
    }
    bytes[0] = 'D';
    bytes[1] = 'S';
    bytes[2] = 'T';
    bytes[3] = 'M';
    bytes[4] = TIME_FILE_VERSION;
    bytes[6] = (uint8_t)offset;
    bytes[7] = (uint8_t)((uint16_t)offset >> 8);
    put32(bytes + 8, utc);
    seal(bytes);
}

static void a_sealed_file_gives_the_utc_time_and_the_offset(void)
{
    uint8_t bytes[TIME_FILE_BYTES];
    HalWallTime time;
    written(bytes, NOVEMBER_2026, -480);
    CHECK(time_file_parse(bytes, &time));
    CHECK_EQUAL(NOVEMBER_2026, time.utc_s);
    CHECK_EQUAL(-480, time.offset_minutes);
    CHECK_EQUAL(HAL_WALL_COMPUTER, time.source);
}

static void the_path_is_in_the_data_folder(void)
{
    static const char EXPECTED[] = "/" FIRMWARE_DATA_DIRECTORY "/time.bin";
    for (unsigned index = 0; index < sizeof EXPECTED; index++) {
        CHECK_EQUAL(EXPECTED[index], TIME_FILE_PATH[index]);
    }
}

static void a_changed_byte_breaks_the_seal(void)
{
    for (int index = 0; index < TIME_FILE_BYTES; index++) {
        uint8_t bytes[TIME_FILE_BYTES];
        HalWallTime time;
        written(bytes, NOVEMBER_2026, 60);
        bytes[index] ^= 0x01;
        CHECK(!time_file_parse(bytes, &time));
        CHECK_EQUAL(HAL_WALL_UNKNOWN, time.source);
    }
}

static void a_resealed_file_still_needs_its_marker_version_and_empty_reserve(void)
{
    static const int CHANGED[] = {0, 3, 4, 5, 12, 27};
    for (unsigned index = 0; index < sizeof CHANGED / sizeof CHANGED[0]; index++) {
        uint8_t bytes[TIME_FILE_BYTES];
        HalWallTime time;
        written(bytes, NOVEMBER_2026, 0);
        bytes[CHANGED[index]] ^= 0x02;
        seal(bytes);
        CHECK(!time_file_parse(bytes, &time));
    }
}

static void times_and_offsets_outside_the_accepted_ranges_are_refused(void)
{
    static const struct {
        uint32_t utc;
        int16_t offset;
        bool accepted;
    } CASES[] = {
        {TIME_FILE_EARLIEST_UTC, 0, true},
        {TIME_FILE_EARLIEST_UTC - 1, 0, false},
        {TIME_FILE_LATEST_UTC, 0, true},
        {TIME_FILE_LATEST_UTC + 1, 0, false},
        {NOVEMBER_2026, TIME_FILE_OFFSET_WEST, true},
        {NOVEMBER_2026, TIME_FILE_OFFSET_WEST - 1, false},
        {NOVEMBER_2026, TIME_FILE_OFFSET_EAST, true},
        {NOVEMBER_2026, TIME_FILE_OFFSET_EAST + 1, false},
    };
    for (unsigned index = 0; index < sizeof CASES / sizeof CASES[0]; index++) {
        uint8_t bytes[TIME_FILE_BYTES];
        HalWallTime time;
        written(bytes, CASES[index].utc, CASES[index].offset);
        CHECK_EQUAL(CASES[index].accepted, time_file_parse(bytes, &time));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_sealed_file_gives_the_utc_time_and_the_offset),
        TEST_CASE(the_path_is_in_the_data_folder),
        TEST_CASE(a_changed_byte_breaks_the_seal),
        TEST_CASE(a_resealed_file_still_needs_its_marker_version_and_empty_reserve),
        TEST_CASE(times_and_offsets_outside_the_accepted_ranges_are_refused),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
