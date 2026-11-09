#include "check.h"
#include "core/frame_pacer.h"

static void first_frame_is_always_due(void)
{
    FramePacer pacer = {0};
    CHECK(frame_pacer_due(&pacer, 0, false));
}

static void active_frames_wait_the_active_interval(void)
{
    FramePacer pacer = {0};
    frame_pacer_done(&pacer, 1000, true);
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS - 1, false));
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
}

static void quiet_frames_slow_the_pace(void)
{
    FramePacer pacer = {0};
    for (int frame = 0; frame < FRAME_PACER_QUIET_FRAMES; frame++) {
        frame_pacer_done(&pacer, 1000, false);
    }
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_IDLE_MS - 1, false));
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_IDLE_MS, false));
}

static void a_few_quiet_frames_keep_the_active_pace(void)
{
    FramePacer pacer = {0};
    for (int frame = 0; frame < FRAME_PACER_QUIET_FRAMES - 1; frame++) {
        frame_pacer_done(&pacer, 1000, false);
    }
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
}

static void a_changed_frame_restores_the_active_pace(void)
{
    FramePacer pacer = {0};
    for (int frame = 0; frame < FRAME_PACER_QUIET_FRAMES + 3; frame++) {
        frame_pacer_done(&pacer, 1000, false);
    }
    frame_pacer_done(&pacer, 2000, true);
    CHECK(frame_pacer_due(&pacer, 2000 + FRAME_PACER_ACTIVE_MS, false));
}

static void urgent_requests_cut_the_idle_wait_short(void)
{
    FramePacer pacer = {0};
    for (int frame = 0; frame < FRAME_PACER_QUIET_FRAMES; frame++) {
        frame_pacer_done(&pacer, 1000, false);
    }
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, false));
    CHECK(!frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS - 1, true));
    CHECK(frame_pacer_due(&pacer, 1000 + FRAME_PACER_ACTIVE_MS, true));
}

static void clock_wrap_does_not_stall_frames(void)
{
    FramePacer pacer = {0};
    frame_pacer_done(&pacer, 0xFFFFFFF0u, true);
    CHECK(frame_pacer_due(&pacer, 0xFFFFFFF0u + FRAME_PACER_ACTIVE_MS, false));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(first_frame_is_always_due),
        TEST_CASE(active_frames_wait_the_active_interval),
        TEST_CASE(quiet_frames_slow_the_pace),
        TEST_CASE(a_few_quiet_frames_keep_the_active_pace),
        TEST_CASE(a_changed_frame_restores_the_active_pace),
        TEST_CASE(urgent_requests_cut_the_idle_wait_short),
        TEST_CASE(clock_wrap_does_not_stall_frames),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
