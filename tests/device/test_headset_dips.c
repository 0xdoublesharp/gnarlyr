#include "check.h"
#include "device/headset_dips.h"

enum {
    SAMPLE_MS = 5,
    IDLE_LEVEL = 1023
};

typedef struct Rig {
    HeadsetDips dips;
    uint32_t now_ms;
} Rig;

static void feed(Rig *rig, uint16_t level, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += SAMPLE_MS) {
        headset_dips_sample(&rig->dips, level, true, rig->now_ms);
        rig->now_ms += SAMPLE_MS;
    }
}

static Rig idle_rig(void)
{
    Rig rig = {.now_ms = 1000};
    feed(&rig, IDLE_LEVEL, 100);
    return rig;
}

static void a_steady_line_shows_no_dip(void)
{
    Rig rig = idle_rig();
    feed(&rig, IDLE_LEVEL - HEADSET_DIPS_MARGIN, 100);
    CHECK(!headset_dips_view(&rig.dips, rig.now_ms).recent);
}

static void a_brief_dip_keeps_its_lowest_reading_and_length(void)
{
    Rig rig = idle_rig();
    feed(&rig, 700, 10);
    feed(&rig, 512, 10);
    feed(&rig, 900, 20);
    feed(&rig, IDLE_LEVEL, 100);
    HeadsetDipView view = headset_dips_view(&rig.dips, rig.now_ms);
    CHECK(view.recent);
    CHECK_EQUAL(512, view.lowest);
    CHECK_EQUAL(40, view.longest_ms);
    CHECK_EQUAL(1, view.count);
}

static void the_dip_is_shown_for_three_seconds(void)
{
    Rig rig = idle_rig();
    feed(&rig, 512, SAMPLE_MS);
    feed(&rig, IDLE_LEVEL, HEADSET_DIPS_HOLD_MS - 2 * SAMPLE_MS);
    CHECK(headset_dips_view(&rig.dips, rig.now_ms).recent);
    feed(&rig, IDLE_LEVEL, 2 * SAMPLE_MS);
    CHECK(!headset_dips_view(&rig.dips, rig.now_ms).recent);
}

static void a_pulse_train_is_counted_as_several_dips(void)
{
    Rig rig = idle_rig();
    for (int pulse = 0; pulse < 3; pulse++) {
        feed(&rig, 600, 10);
        feed(&rig, IDLE_LEVEL, 30);
    }
    HeadsetDipView view = headset_dips_view(&rig.dips, rig.now_ms);
    CHECK_EQUAL(3, view.count);
    CHECK_EQUAL(10, view.longest_ms);
}

static void a_dip_after_the_hold_starts_a_new_count(void)
{
    Rig rig = idle_rig();
    feed(&rig, 600, 10);
    feed(&rig, IDLE_LEVEL, HEADSET_DIPS_HOLD_MS + 100);
    feed(&rig, 800, 10);
    HeadsetDipView view = headset_dips_view(&rig.dips, rig.now_ms);
    CHECK_EQUAL(1, view.count);
    CHECK_EQUAL(800, view.lowest);
}

static void a_level_held_over_a_second_becomes_the_new_idle(void)
{
    Rig rig = idle_rig();
    feed(&rig, 30, 1500);
    HeadsetDipView view = headset_dips_view(&rig.dips, rig.now_ms);
    CHECK(view.recent);
    CHECK_EQUAL(30, view.lowest);
    CHECK_EQUAL(HEADSET_DIPS_LONGEST_MS, view.longest_ms);
    feed(&rig, 30, HEADSET_DIPS_HOLD_MS);
    CHECK(!headset_dips_view(&rig.dips, rig.now_ms).recent);
}

static void going_inactive_forgets_the_idle_level_and_the_dips(void)
{
    Rig rig = idle_rig();
    feed(&rig, 512, 20);
    headset_dips_sample(&rig.dips, 0, false, rig.now_ms);
    CHECK(!headset_dips_view(&rig.dips, rig.now_ms).recent);
    feed(&rig, 0, 2000);
    CHECK(!headset_dips_view(&rig.dips, rig.now_ms).recent);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_steady_line_shows_no_dip),
        TEST_CASE(a_brief_dip_keeps_its_lowest_reading_and_length),
        TEST_CASE(the_dip_is_shown_for_three_seconds),
        TEST_CASE(a_pulse_train_is_counted_as_several_dips),
        TEST_CASE(a_dip_after_the_hold_starts_a_new_count),
        TEST_CASE(a_level_held_over_a_second_becomes_the_new_idle),
        TEST_CASE(going_inactive_forgets_the_idle_level_and_the_dips),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
