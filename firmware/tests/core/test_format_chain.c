#include <string.h>

#include "check.h"
#include "core/format_chain.h"

enum {
    VERSION_AT = 2,
    VALUE_AT = 3,
    SCALE_AT = 4,
    FLAGS_AT = 6,
    V1_BYTES = 4,
    V2_BYTES = 6,
    V3_BYTES = 7,
    DEFAULT_SCALE = 100,
    CAPACITY = 16
};

static int steps_run;

static bool upgrade_v1_to_v2(FormatBytes *file)
{
    if (file->capacity < V2_BYTES || file->length != V1_BYTES) {
        return false;
    }
    file->bytes[VERSION_AT] = 2;
    file->bytes[SCALE_AT] = DEFAULT_SCALE;
    file->bytes[SCALE_AT + 1] = 0;
    file->length = V2_BYTES;
    steps_run++;
    return true;
}

static bool upgrade_v2_to_v3(FormatBytes *file)
{
    if (file->capacity < V3_BYTES || file->length != V2_BYTES) {
        return false;
    }
    file->bytes[VERSION_AT] = 3;
    file->bytes[FLAGS_AT] = 0;
    file->length = V3_BYTES;
    steps_run += 10;
    return true;
}

static const FormatUpgrade FAKE_STEPS[] = {upgrade_v1_to_v2, upgrade_v2_to_v3};
static const FormatChain FAKE_V2 = {.current = 2, .steps = FAKE_STEPS};
static const FormatChain FAKE_V3 = {.current = 3, .steps = FAKE_STEPS};

static FormatBytes fake_file(uint8_t *bytes, uint8_t version, uint32_t length)
{
    memset(bytes, 0xEE, CAPACITY);
    bytes[0] = 'F';
    bytes[1] = 'K';
    bytes[VERSION_AT] = version;
    bytes[VALUE_AT] = 42;
    steps_run = 0;
    return (FormatBytes){.bytes = bytes, .length = length, .capacity = CAPACITY};
}

static void versions_are_ordered_against_the_current_one(void)
{
    CHECK_EQUAL(FORMAT_AGE_INVALID, format_age(2, 0));
    CHECK_EQUAL(FORMAT_AGE_OLDER, format_age(2, 1));
    CHECK_EQUAL(FORMAT_AGE_CURRENT, format_age(2, 2));
    CHECK_EQUAL(FORMAT_AGE_NEWER, format_age(2, 3));
    CHECK_EQUAL(FORMAT_AGE_CURRENT, format_age(1, 1));
    CHECK_EQUAL(FORMAT_AGE_NEWER, format_age(1, 2));
}

static void a_fake_v2_reads_a_v1_file_through_its_one_step(void)
{
    uint8_t bytes[CAPACITY];
    FormatBytes file = fake_file(bytes, 1, V1_BYTES);
    CHECK(format_upgrade(&FAKE_V2, 1, &file));
    CHECK_EQUAL(1, steps_run);
    CHECK_EQUAL(2, bytes[VERSION_AT]);
    CHECK_EQUAL(42, bytes[VALUE_AT]);
    CHECK_EQUAL(DEFAULT_SCALE, bytes[SCALE_AT]);
    CHECK_EQUAL(V2_BYTES, file.length);
}

static void a_v1_file_climbs_every_step_to_v3_in_order(void)
{
    uint8_t bytes[CAPACITY];
    FormatBytes file = fake_file(bytes, 1, V1_BYTES);
    CHECK(format_upgrade(&FAKE_V3, 1, &file));
    CHECK_EQUAL(11, steps_run);
    CHECK_EQUAL(3, bytes[VERSION_AT]);
    CHECK_EQUAL(42, bytes[VALUE_AT]);
    CHECK_EQUAL(DEFAULT_SCALE, bytes[SCALE_AT]);
    CHECK_EQUAL(0, bytes[FLAGS_AT]);
    CHECK_EQUAL(V3_BYTES, file.length);
}

static void a_v2_file_takes_only_the_later_step(void)
{
    uint8_t bytes[CAPACITY];
    FormatBytes file = fake_file(bytes, 2, V2_BYTES);
    CHECK(format_upgrade(&FAKE_V3, 2, &file));
    CHECK_EQUAL(10, steps_run);
    CHECK_EQUAL(3, bytes[VERSION_AT]);
}

static void a_current_file_runs_no_step(void)
{
    uint8_t bytes[CAPACITY];
    FormatBytes file = fake_file(bytes, 2, V2_BYTES);
    CHECK(format_upgrade(&FAKE_V2, 2, &file));
    CHECK_EQUAL(0, steps_run);
    CHECK_EQUAL(V2_BYTES, file.length);
}

static void a_newer_file_is_refused_and_left_as_it_was(void)
{
    uint8_t bytes[CAPACITY];
    uint8_t before[CAPACITY];
    FormatBytes file = fake_file(bytes, 3, V3_BYTES);
    memcpy(before, bytes, sizeof before);
    CHECK(!format_upgrade(&FAKE_V2, 3, &file));
    CHECK_EQUAL(0, memcmp(before, bytes, sizeof before));
    CHECK_EQUAL(V3_BYTES, file.length);
}

static void version_zero_is_never_upgraded(void)
{
    uint8_t bytes[CAPACITY];
    FormatBytes file = fake_file(bytes, 0, V1_BYTES);
    CHECK(!format_upgrade(&FAKE_V3, 0, &file));
    CHECK_EQUAL(0, steps_run);
}

static void a_step_that_fails_stops_the_chain(void)
{
    uint8_t bytes[CAPACITY];
    FormatBytes file = fake_file(bytes, 1, V1_BYTES);
    file.capacity = V2_BYTES;
    CHECK(!format_upgrade(&FAKE_V3, 1, &file));
    CHECK_EQUAL(1, steps_run);
}

static void a_chain_without_its_steps_refuses_older_files(void)
{
    uint8_t bytes[CAPACITY];
    FormatChain missing = {.current = 2, .steps = NULL};
    FormatBytes file = fake_file(bytes, 1, V1_BYTES);
    CHECK(!format_upgrade(&missing, 1, &file));
    CHECK(format_upgrade(&missing, 2, &file));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(versions_are_ordered_against_the_current_one),
        TEST_CASE(a_fake_v2_reads_a_v1_file_through_its_one_step),
        TEST_CASE(a_v1_file_climbs_every_step_to_v3_in_order),
        TEST_CASE(a_v2_file_takes_only_the_later_step),
        TEST_CASE(a_current_file_runs_no_step),
        TEST_CASE(a_newer_file_is_refused_and_left_as_it_was),
        TEST_CASE(version_zero_is_never_upgraded),
        TEST_CASE(a_step_that_fails_stops_the_chain),
        TEST_CASE(a_chain_without_its_steps_refuses_older_files),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
