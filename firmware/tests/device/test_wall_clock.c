#include "check.h"
#include "device/tick_clock.h"
#include "device/wall_clock.h"

enum {
    NOVEMBER_2026 = 1794482460u,
    CRYSTAL_TICKS = 24,
    COAST_TICKS = 48,
    FULL_TICKS = 96
};

static HalWallTime computer_time(uint32_t utc, int16_t offset)
{
    return (HalWallTime){.utc_s = utc, .offset_minutes = offset, .source = HAL_WALL_COMPUTER};
}

static void a_clock_that_was_never_set_is_unknown(void)
{
    WallClock clock = {0};
    HalWallTime time = wall_clock_now(&clock, 123456);
    CHECK_EQUAL(HAL_WALL_UNKNOWN, time.source);
    CHECK_EQUAL(0, time.utc_s);
}

static void a_set_clock_counts_whole_seconds_from_the_moment_it_was_set(void)
{
    WallClock clock;
    wall_clock_set(&clock, computer_time(NOVEMBER_2026, 60), 5000);
    CHECK_EQUAL(NOVEMBER_2026, wall_clock_now(&clock, 5999).utc_s);
    CHECK_EQUAL(NOVEMBER_2026 + 1, wall_clock_now(&clock, 6000).utc_s);
    CHECK_EQUAL(NOVEMBER_2026 + 3600, wall_clock_now(&clock, 5000 + 3600000).utc_s);
    HalWallTime time = wall_clock_now(&clock, 5000 + 3600999);
    CHECK_EQUAL(NOVEMBER_2026 + 3600, time.utc_s);
    CHECK_EQUAL(60, time.offset_minutes);
    CHECK_EQUAL(HAL_WALL_COMPUTER, time.source);
}

static void reading_often_never_loses_the_part_of_a_second_between_reads(void)
{
    WallClock clock;
    wall_clock_set(&clock, computer_time(NOVEMBER_2026, 0), 0);
    for (uint32_t now = 0; now <= 100000; now += 333) {
        CHECK_EQUAL(NOVEMBER_2026 + now / 1000, wall_clock_now(&clock, now).utc_s);
    }
}

static void the_millisecond_counter_wrapping_keeps_the_time_right(void)
{
    WallClock clock;
    uint32_t set_ms = UINT32_MAX - 1500;
    wall_clock_set(&clock, computer_time(NOVEMBER_2026, 0), set_ms);
    CHECK_EQUAL(NOVEMBER_2026 + 3, wall_clock_now(&clock, set_ms + 3000).utc_s);
}

static void an_uptime_of_a_year_read_once_a_day_stays_exact(void)
{
    WallClock clock;
    uint32_t now = 777;
    wall_clock_set(&clock, computer_time(NOVEMBER_2026, 0), now);
    for (uint32_t day = 1; day <= 366; day++) {
        now += 86400000u;
        CHECK_EQUAL(NOVEMBER_2026 + day * 86400u, wall_clock_now(&clock, now).utc_s);
    }
}

static TickReading counter_at(uint32_t counter)
{
    return (TickReading){.counter = counter, .reload_pending = false};
}

static void run_periods(TickClock *ticks, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        tick_clock_reload(ticks);
    }
}

static void core_clock_changes_and_coasting_do_not_skew_the_time(void)
{
    TickClock ticks;
    WallClock clock;
    tick_clock_init(&ticks, CRYSTAL_TICKS, CRYSTAL_TICKS * 1000);
    wall_clock_set(&clock, computer_time(NOVEMBER_2026, 0),
                   tick_clock_milliseconds(&ticks, counter_at(CRYSTAL_TICKS * 1000)));
    run_periods(&ticks, 2500);
    tick_clock_set_rate(&ticks, counter_at(CRYSTAL_TICKS * 500), FULL_TICKS, FULL_TICKS * 1000);
    run_periods(&ticks, 2500);
    tick_clock_set_rate(&ticks, counter_at(FULL_TICKS * 500), COAST_TICKS, COAST_TICKS * 1000);
    run_periods(&ticks, 5000);
    uint32_t now = tick_clock_milliseconds(&ticks, counter_at(COAST_TICKS * 1000));
    CHECK_EQUAL(10001, now);
    CHECK_EQUAL(NOVEMBER_2026 + 10, wall_clock_now(&clock, now).utc_s);
}

static void setting_again_replaces_the_time_and_the_offset(void)
{
    WallClock clock;
    wall_clock_set(&clock, computer_time(NOVEMBER_2026, 60), 0);
    wall_clock_set(&clock, computer_time(NOVEMBER_2026 + 7200, -300), 50000);
    HalWallTime time = wall_clock_now(&clock, 51000);
    CHECK_EQUAL(NOVEMBER_2026 + 7201, time.utc_s);
    CHECK_EQUAL(-300, time.offset_minutes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_clock_that_was_never_set_is_unknown),
        TEST_CASE(a_set_clock_counts_whole_seconds_from_the_moment_it_was_set),
        TEST_CASE(reading_often_never_loses_the_part_of_a_second_between_reads),
        TEST_CASE(the_millisecond_counter_wrapping_keeps_the_time_right),
        TEST_CASE(an_uptime_of_a_year_read_once_a_day_stays_exact),
        TEST_CASE(core_clock_changes_and_coasting_do_not_skew_the_time),
        TEST_CASE(setting_again_replaces_the_time_and_the_offset),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
