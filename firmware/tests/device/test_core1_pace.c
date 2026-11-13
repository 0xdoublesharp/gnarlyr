#include "check.h"
#include "device/clock_governor.h"
#include "device/core1_pace.h"

static ClockGovernorReading at(uint32_t now, uint32_t idle)
{
    return (ClockGovernorReading){.now_us = now, .idle = idle};
}

static void units_are_hundred_megahertz_cycles_at_every_clock(void)
{
    CHECK_EQUAL(1000u, core1_pace_units(1000, 100));
    CHECK_EQUAL(500u, core1_pace_units(1000, 200));
    CHECK_EQUAL(2000u, core1_pace_units(1000, 50));
    CHECK_EQUAL(4000u, core1_pace_units(1000, 25));
}

static void core_1_rests_longer_only_while_it_waits_for_room(void)
{
    CHECK_EQUAL(5000u, core1_pace_rest_us(false));
    CHECK_EQUAL(20000u, core1_pace_rest_us(true));
}

static void core_1_rests_at_no_more_than_100_megahertz(void)
{
    CHECK_EQUAL(100u, core1_pace_rest_mhz(200));
    CHECK_EQUAL(100u, core1_pace_rest_mhz(100));
    CHECK_EQUAL(25u, core1_pace_rest_mhz(25));
}

static void each_clock_maps_to_its_divider_field(void)
{
    CHECK_EQUAL(0u, core1_pace_divider_field(200));
    CHECK_EQUAL(1u, core1_pace_divider_field(100));
    CHECK_EQUAL(3u, core1_pace_divider_field(50));
    CHECK_EQUAL(7u, core1_pace_divider_field(25));
    CHECK_EQUAL(25u, core1_pace_mhz(8));
}

static void the_requested_clock_is_the_ceiling(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, core1_pace_limits(200), at(0, 0));
    CHECK_EQUAL(200u, core1_pace_mhz(governor.divider));
    clock_governor_start(&governor, core1_pace_limits(100), at(0, 0));
    CHECK_EQUAL(100u, core1_pace_mhz(governor.divider));
    CHECK(!clock_governor_update(&governor, at(50000000, 0)));
    CHECK_EQUAL(100u, core1_pace_mhz(governor.divider));
}

static void a_light_load_steps_down_to_25_megahertz_and_hurrying_restores_the_ceiling(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, core1_pace_limits(100), at(0, 0));
    CHECK(clock_governor_update(&governor, at(50000000, 46000000)));
    CHECK_EQUAL(50u, core1_pace_mhz(governor.divider));
    CHECK(clock_governor_update(&governor, at(100000000, 92000000)));
    CHECK_EQUAL(25u, core1_pace_mhz(governor.divider));
    CHECK(!clock_governor_update(&governor, at(150000000, 138000000)));
    CHECK_EQUAL(25u, core1_pace_mhz(governor.divider));
    clock_governor_hurry(&governor, at(160000000, 140000000));
    CHECK_EQUAL(100u, core1_pace_mhz(governor.divider));
}

static void the_window_is_counted_in_units_across_the_32_bit_wrap(void)
{
    ClockGovernor governor;
    clock_governor_start(&governor, core1_pace_limits(100), at(0xFFFFFF00u, 0xFFFFFF00u));
    CHECK(clock_governor_update(&governor, at(50000000u - 0x100u, 46000000u - 0x100u)));
    CHECK_EQUAL(50u, core1_pace_mhz(governor.divider));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(units_are_hundred_megahertz_cycles_at_every_clock),
        TEST_CASE(core_1_rests_longer_only_while_it_waits_for_room),
        TEST_CASE(core_1_rests_at_no_more_than_100_megahertz),
        TEST_CASE(each_clock_maps_to_its_divider_field),
        TEST_CASE(the_requested_clock_is_the_ceiling),
        TEST_CASE(a_light_load_steps_down_to_25_megahertz_and_hurrying_restores_the_ceiling),
        TEST_CASE(the_window_is_counted_in_units_across_the_32_bit_wrap),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
