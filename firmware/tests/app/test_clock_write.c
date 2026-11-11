#include "check.h"
#include "extension_entry.h"
#include "hal/clock.h"
#include "hal/system.h"

static bool loaded;
static bool accepted;
static int writes;
static HalWallTime written;

static bool set_clock(HalWallTime time)
{
    writes++;
    written = time;
    return accepted;
}

static bool restore_clock(int16_t offset)
{
    writes++;
    written.offset_minutes = offset;
    return accepted;
}

static const ExtensionModuleEntry code = {.clock_set = set_clock, .clock_restore = restore_clock};

const void *hal_system_extension_code(void)
{
    return loaded ? &code : NULL;
}

static void missing_extension_refuses_without_writing(void)
{
    loaded = false;
    writes = 0;
    CHECK(!hal_clock_set_wall((HalWallTime){.utc_s = 1794390060u}));
    CHECK(!hal_clock_restore_wall(-480));
    CHECK_EQUAL(0, writes);
}

static void the_write_result_and_time_pass_through_the_extension(void)
{
    loaded = true;
    accepted = false;
    writes = 0;
    HalWallTime wanted = {.utc_s = 1794390060u, .offset_minutes = -480, .source = HAL_WALL_PLAYER};
    CHECK(!hal_clock_set_wall(wanted));
    CHECK_EQUAL(1, writes);
    accepted = true;
    CHECK(hal_clock_set_wall(wanted));
    CHECK_EQUAL(2, writes);
    CHECK_EQUAL(wanted.utc_s, written.utc_s);
    CHECK_EQUAL(wanted.offset_minutes, written.offset_minutes);
    CHECK(hal_clock_restore_wall(345));
    CHECK_EQUAL(345, written.offset_minutes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(missing_extension_refuses_without_writing),
        TEST_CASE(the_write_result_and_time_pass_through_the_extension),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
