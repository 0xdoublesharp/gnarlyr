#include "check.h"
#include "device/clock_governor.h"

static const ClockGovernorLimits CORE0 = {.window_us = 500000,
                                          .lowest_divider = 2,
                                          .highest_divider = 8,
                                          .deepen_percent = 50,
                                          .raise_percent = 70};
static const ClockGovernorLimits CORE1 = {.window_us = 500000,
                                          .pll_mhz = 200,
                                          .lowest_divider = 2,
                                          .highest_divider = 8,
                                          .deepen_percent = 60,
                                          .raise_percent = 80};

static ClockGovernorReading at(uint32_t now_us, uint32_t idle)
{
    return (ClockGovernorReading){.now_us = now_us, .idle = idle};
}

static void a_governor_starts_at_the_fastest_divider(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(1000, 0));
    CHECK_EQUAL(2u, governor.divider);
}

static void nothing_changes_before_the_window_ends(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0, 0));
    CHECK(!clock_governor_update(&governor, at(499999, 499999)));
    CHECK_EQUAL(2u, governor.divider);
}

static void a_light_load_halves_the_clock_one_step_per_window(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0, 0));
    CHECK(clock_governor_update(&governor, at(500000, 400000)));
    CHECK_EQUAL(20u, governor.busy_percent);
    CHECK_EQUAL(4u, governor.divider);
    CHECK(clock_governor_update(&governor, at(1000000, 800000)));
    CHECK_EQUAL(8u, governor.divider);
    CHECK(!clock_governor_update(&governor, at(1500000, 1200000)));
    CHECK_EQUAL(8u, governor.divider);
}

static void a_load_between_the_limits_keeps_the_clock(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0, 0));
    CHECK(!clock_governor_update(&governor, at(500000, 300000)));
    CHECK_EQUAL(40u, governor.busy_percent);
    CHECK_EQUAL(2u, governor.divider);
}

static void a_heavy_load_doubles_the_clock(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0, 0));
    clock_governor_update(&governor, at(500000, 450000));
    clock_governor_update(&governor, at(1000000, 900000));
    CHECK_EQUAL(8u, governor.divider);
    CHECK(clock_governor_update(&governor, at(1500000, 1000000)));
    CHECK_EQUAL(80u, governor.busy_percent);
    CHECK_EQUAL(4u, governor.divider);
}

static void the_clock_never_rises_past_the_fastest_divider(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0, 0));
    CHECK(!clock_governor_update(&governor, at(500000, 0)));
    CHECK_EQUAL(100u, governor.busy_percent);
    CHECK_EQUAL(2u, governor.divider);
}

static void hurrying_returns_to_the_fastest_divider_and_restarts_the_window(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0, 0));
    clock_governor_update(&governor, at(500000, 490000));
    clock_governor_hurry(&governor, at(600000, 590000));
    CHECK_EQUAL(2u, governor.divider);
    CHECK(!clock_governor_update(&governor, at(1099999, 1089999)));
}

static void idle_counted_in_cycles_follows_the_divided_clock(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE1, at(0, 0));
    CHECK(clock_governor_update(&governor, at(500000, 45000000u)));
    CHECK_EQUAL(10u, governor.busy_percent);
    CHECK_EQUAL(4u, governor.divider);
    CHECK(!clock_governor_update(&governor, at(1000000, 45000000u + 15000000u)));
    CHECK_EQUAL(40u, governor.busy_percent);
    CHECK_EQUAL(4u, governor.divider);
}

static void the_counters_may_wrap(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, &CORE0, at(0xFFFF0000u, 0xFFFFF000u));
    CHECK(clock_governor_update(&governor, at(0xFFFF0000u + 500000u, 0xFFFFF000u + 450000u)));
    CHECK_EQUAL(10u, governor.busy_percent);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_governor_starts_at_the_fastest_divider),
        TEST_CASE(nothing_changes_before_the_window_ends),
        TEST_CASE(a_light_load_halves_the_clock_one_step_per_window),
        TEST_CASE(a_load_between_the_limits_keeps_the_clock),
        TEST_CASE(a_heavy_load_doubles_the_clock),
        TEST_CASE(the_clock_never_rises_past_the_fastest_divider),
        TEST_CASE(hurrying_returns_to_the_fastest_divider_and_restarts_the_window),
        TEST_CASE(idle_counted_in_cycles_follows_the_divided_clock),
        TEST_CASE(the_counters_may_wrap),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
