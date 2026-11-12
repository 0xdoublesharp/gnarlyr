#include "check.h"
#include "device/power_history.h"

static PowerHistory history_with(uint32_t count)
{
    PowerHistory history = {.recorded = 0};
    for (uint32_t index = 0; index < count; index++) {
        power_history_record(&history, (PowerSample){.minute = (uint16_t)index,
                                                     .millivolts = (uint16_t)(3500 + index),
                                                     .percent = 50,
                                                     .flags = POWER_SAMPLE_VBUS});
    }
    return history;
}

static void an_empty_history_holds_nothing(void)
{
    PowerHistory history = history_with(0);
    CHECK_EQUAL(0, power_history_count(&history));
}

static void samples_come_back_oldest_first(void)
{
    PowerHistory history = history_with(3);
    CHECK_EQUAL(3, power_history_count(&history));
    CHECK_EQUAL(0, power_history_at(&history, 0).minute);
    CHECK_EQUAL(3502, power_history_at(&history, 2).millivolts);
}

static void a_full_history_keeps_the_newest_samples(void)
{
    PowerHistory history = history_with(POWER_HISTORY_SAMPLES + 7);
    CHECK_EQUAL(POWER_HISTORY_SAMPLES, power_history_count(&history));
    CHECK_EQUAL(7, power_history_at(&history, 0).minute);
    CHECK_EQUAL(POWER_HISTORY_SAMPLES + 6,
                power_history_at(&history, POWER_HISTORY_SAMPLES - 1).minute);
}

static void the_history_covers_more_than_two_hours(void)
{
    CHECK((POWER_HISTORY_SAMPLES - 1) * POWER_HISTORY_INTERVAL_S >= 2 * 60 * 60);
    CHECK_EQUAL(60, POWER_HISTORY_INTERVAL_S);
    CHECK_EQUAL(128, POWER_HISTORY_SAMPLES);
}

static void a_sample_is_six_bytes(void)
{
    CHECK_EQUAL(6, sizeof(PowerSample));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_empty_history_holds_nothing),
        TEST_CASE(samples_come_back_oldest_first),
        TEST_CASE(a_full_history_keeps_the_newest_samples),
        TEST_CASE(the_history_covers_more_than_two_hours),
        TEST_CASE(a_sample_is_six_bytes),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
