#include "check.h"
#include "core/frame_pacer.h"

static void quiet(FramePacer *pacer, uint32_t now_ms, int frames)
{
    for (int frame = 0; frame < frames; frame++) {
        frame_pacer_done(pacer, now_ms, false, false);
    }
}

static void first_frame_is_always_due(void)
{
    FramePacer pacer = {0};
    CHECK(frame_pacer_due(&pacer, 0, false));
}

static void after_input_frames_come_at_the_active_interval(void)
{
    FramePacer pacer = {0};
    frame_pacer_done(&pacer, 1000, true, true);
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS - 1, false));
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
}

static void the_active_pace_lasts_a_few_unchanged_frames_after_input(void)
{
    FramePacer pacer = {0};
    frame_pacer_done(&pacer, 1000, true, true);
    quiet(&pacer, 1000, FRAME_PACER_FAST_FRAMES - 1);
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
    quiet(&pacer, 1000, 1);
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
}

static void three_changed_frames_in_a_row_are_an_animation_and_get_the_active_pace(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, false);
    frame_pacer_done(&pacer, 2100, true, false);
    CHECK(!frame_pacer_due(&pacer, 2100 + FRAME_PACER_ACTIVE_MS, false));
    frame_pacer_done(&pacer, 2200, true, false);
    CHECK(frame_pacer_due(&pacer, 2200 + FRAME_PACER_ACTIVE_MS, false));
}

static void changes_with_an_unchanged_frame_between_them_are_not_an_animation(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    for (uint32_t tick = 0; tick < 5; tick++) {
        frame_pacer_done(&pacer, 2000 + tick * 300, true, false);
        frame_pacer_done(&pacer, 2100 + tick * 300, true, false);
        frame_pacer_done(&pacer, 2200 + tick * 300, false, false);
    }
    CHECK(!frame_pacer_due(&pacer, 3400 + FRAME_PACER_ACTIVE_MS, false));
}

static void one_changed_frame_without_input_does_not_start_the_active_pace(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, false);
    CHECK(!frame_pacer_due(&pacer, 2000 + FRAME_PACER_IDLE_MS - 1, false));
    CHECK(frame_pacer_due(&pacer, 2000 + FRAME_PACER_IDLE_MS, false));
}

static void after_a_change_two_checks_come_at_the_idle_interval_and_then_the_screen_rests(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, false);
    quiet(&pacer, 2100, 1);
    CHECK(frame_pacer_due(&pacer, 2100 + FRAME_PACER_IDLE_MS, false));
    quiet(&pacer, 2200, 1);
    CHECK(!frame_pacer_due(&pacer, 2200 + FRAME_PACER_REST_MS - 1, false));
    CHECK(frame_pacer_due(&pacer, 2200 + FRAME_PACER_REST_MS, false));
}

static void an_urgent_request_cuts_the_rest_short(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS - 1, true));
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, true));
}

static void clock_wrap_does_not_stall_frames(void)
{
    FramePacer pacer = {0};
    frame_pacer_done(&pacer, 0xFFFFFFF0u, true, true);
    CHECK(frame_pacer_due(&pacer, 0xFFFFFFF0u + FRAME_PACER_ACTIVE_MS, false));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(first_frame_is_always_due),
        TEST_CASE(after_input_frames_come_at_the_active_interval),
        TEST_CASE(the_active_pace_lasts_a_few_unchanged_frames_after_input),
        TEST_CASE(three_changed_frames_in_a_row_are_an_animation_and_get_the_active_pace),
        TEST_CASE(changes_with_an_unchanged_frame_between_them_are_not_an_animation),
        TEST_CASE(one_changed_frame_without_input_does_not_start_the_active_pace),
        TEST_CASE(after_a_change_two_checks_come_at_the_idle_interval_and_then_the_screen_rests),
        TEST_CASE(an_urgent_request_cuts_the_rest_short),
        TEST_CASE(clock_wrap_does_not_stall_frames),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
