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

static void three_changed_frames_in_a_row_at_the_active_pace_keep_it(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, true);
    quiet(&pacer, 2000, FRAME_PACER_FAST_FRAMES - 1);
    frame_pacer_done(&pacer, 2100, true, false);
    frame_pacer_done(&pacer, 2116, true, false);
    frame_pacer_done(&pacer, 2132, true, false);
    quiet(&pacer, 2132, FRAME_PACER_FAST_FRAMES - 1);
    CHECK(frame_pacer_due(&pacer, 2132 + FRAME_PACER_ACTIVE_MS, false));
}

static void changes_at_the_idle_pace_never_start_the_active_pace(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    for (uint32_t frame = 0; frame < 20; frame++) {
        frame_pacer_done(&pacer, 2000 + frame * FRAME_PACER_IDLE_MS, true, false);
    }
    uint32_t last_ms = 2000 + 19 * FRAME_PACER_IDLE_MS;
    CHECK(!frame_pacer_due(&pacer, last_ms + FRAME_PACER_ACTIVE_MS, false));
    CHECK(frame_pacer_due(&pacer, last_ms + FRAME_PACER_IDLE_MS, false));
}

static void a_change_every_few_active_frames_after_input_returns_to_the_idle_pace(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, true);
    for (uint32_t frame = 1; frame <= 30; frame++) {
        frame_pacer_done(&pacer, 2000 + frame * FRAME_PACER_ACTIVE_MS, frame % 5 == 0, false);
    }
    CHECK(
        !frame_pacer_due(&pacer, 2000 + 30 * FRAME_PACER_ACTIVE_MS + FRAME_PACER_ACTIVE_MS, false));
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

static void after_a_change_one_check_comes_at_the_idle_interval_and_then_the_screen_rests(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, false);
    CHECK(frame_pacer_due(&pacer, 2000 + FRAME_PACER_IDLE_MS, false));
    quiet(&pacer, 2100, FRAME_PACER_IDLE_FRAMES);
    CHECK(!frame_pacer_due(&pacer, 2100 + FRAME_PACER_REST_MS - 1, false));
    CHECK(frame_pacer_due(&pacer, 2100 + FRAME_PACER_REST_MS, false));
}

static void unchanged_frames_soon_after_a_change_still_owe_the_idle_check(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, true);
    for (uint32_t frame = 1; frame <= FRAME_PACER_FAST_FRAMES; frame++) {
        frame_pacer_done(&pacer, 2000 + frame * FRAME_PACER_ACTIVE_MS, false, false);
    }
    uint32_t last_ms = 2000 + FRAME_PACER_FAST_FRAMES * FRAME_PACER_ACTIVE_MS;
    CHECK(!frame_pacer_due(&pacer, last_ms + FRAME_PACER_ACTIVE_MS, false));
    CHECK(frame_pacer_due(&pacer, last_ms + FRAME_PACER_IDLE_MS, false));
}

static void a_frame_unchanged_soon_after_a_passive_change_keeps_the_idle_check(void)
{
    FramePacer pacer = {0};
    quiet(&pacer, 1000, 20);
    frame_pacer_done(&pacer, 2000, true, false);
    frame_pacer_done(&pacer, 2050, false, false);
    CHECK(frame_pacer_due(&pacer, 2050 + FRAME_PACER_IDLE_MS, false));
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

static void quiet_frame_count_saturates_without_returning_to_idle_pacing(void)
{
    FramePacer pacer = {.quiet_frames = UINT32_MAX, .started = true};
    frame_pacer_done(&pacer, 1000, false, false);
    CHECK_EQUAL(UINT32_MAX, pacer.quiet_frames);
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_IDLE_MS, false));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(first_frame_is_always_due),
        TEST_CASE(after_input_frames_come_at_the_active_interval),
        TEST_CASE(the_active_pace_lasts_a_few_unchanged_frames_after_input),
        TEST_CASE(three_changed_frames_in_a_row_at_the_active_pace_keep_it),
        TEST_CASE(changes_at_the_idle_pace_never_start_the_active_pace),
        TEST_CASE(a_change_every_few_active_frames_after_input_returns_to_the_idle_pace),
        TEST_CASE(changes_with_an_unchanged_frame_between_them_are_not_an_animation),
        TEST_CASE(one_changed_frame_without_input_does_not_start_the_active_pace),
        TEST_CASE(after_a_change_one_check_comes_at_the_idle_interval_and_then_the_screen_rests),
        TEST_CASE(unchanged_frames_soon_after_a_change_still_owe_the_idle_check),
        TEST_CASE(a_frame_unchanged_soon_after_a_passive_change_keeps_the_idle_check),
        TEST_CASE(an_urgent_request_cuts_the_rest_short),
        TEST_CASE(clock_wrap_does_not_stall_frames),
        TEST_CASE(quiet_frame_count_saturates_without_returning_to_idle_pacing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
