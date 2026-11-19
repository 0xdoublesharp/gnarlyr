#include "check.h"
#include "ui/marquee.h"

enum {
    LONG_TEXT = 200,
    VIEW = 100,
    PAUSE_MS = 1000,
    SPEED = 30,
    GAP = 20,
    SCROLL_MS = (LONG_TEXT + GAP) * 1000 / SPEED,
    CYCLE_MS = PAUSE_MS + SCROLL_MS
};

static const MarqueeTiming TIMING = {.pause_ms = PAUSE_MS, .speed_px_per_s = SPEED, .gap_px = GAP};

static void text_that_fits_never_scrolls(void)
{
    CHECK_EQUAL(0, marquee_offset(TIMING, 50, VIEW, 5000));
    CHECK_EQUAL(0, marquee_offset(TIMING, VIEW, VIEW, 5000));
}

static void scrolling_waits_for_the_pause(void)
{
    CHECK_EQUAL(0, marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS - 1));
}

static void scrolling_moves_at_the_configured_speed(void)
{
    CHECK_EQUAL(30, marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + 1000));
}

static void a_full_cycle_returns_to_the_start_and_pauses_again(void)
{
    CHECK_EQUAL(0, marquee_offset(TIMING, LONG_TEXT, VIEW, CYCLE_MS));
    CHECK_EQUAL(0, marquee_offset(TIMING, LONG_TEXT, VIEW, CYCLE_MS + PAUSE_MS - 1));
    CHECK_EQUAL(30, marquee_offset(TIMING, LONG_TEXT, VIEW, CYCLE_MS + PAUSE_MS + 1000));
}

static void the_offset_stays_within_one_text_and_gap(void)
{
    int offset = marquee_offset(TIMING, LONG_TEXT, VIEW, CYCLE_MS - 1);
    CHECK(offset > 0);
    CHECK(offset < LONG_TEXT + GAP);
}

static void the_offset_moves_in_steps_of_the_marquee_pace(void)
{
    int step = SPEED * MARQUEE_STEP_MS / 1000;
    CHECK_EQUAL(0, marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + MARQUEE_STEP_MS - 1));
    CHECK_EQUAL(step, marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + MARQUEE_STEP_MS));
    CHECK_EQUAL(step, marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + 2 * MARQUEE_STEP_MS - 1));
    CHECK_EQUAL(2 * step, marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + 2 * MARQUEE_STEP_MS));
}

static void text_that_fits_never_asks_for_a_step(void)
{
    CHECK_EQUAL(MARQUEE_STILL, marquee_wait_ms(TIMING, 50, VIEW, 5000));
    MarqueeTiming still = {.pause_ms = PAUSE_MS, .speed_px_per_s = 0, .gap_px = GAP};
    CHECK_EQUAL(MARQUEE_STILL, marquee_wait_ms(still, LONG_TEXT, VIEW, 5000));
}

static void the_first_step_is_due_one_step_after_the_pause(void)
{
    CHECK_EQUAL(PAUSE_MS + MARQUEE_STEP_MS, marquee_wait_ms(TIMING, LONG_TEXT, VIEW, 0));
    CHECK_EQUAL(MARQUEE_STEP_MS + 300, marquee_wait_ms(TIMING, LONG_TEXT, VIEW, PAUSE_MS - 300));
    CHECK_EQUAL(1u, marquee_wait_ms(TIMING, LONG_TEXT, VIEW, PAUSE_MS + MARQUEE_STEP_MS - 1));
    CHECK(marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + MARQUEE_STEP_MS) > 0);
}

static void a_moving_marquee_is_due_at_its_next_step_and_at_the_end_of_its_cycle(void)
{
    CHECK_EQUAL(MARQUEE_STEP_MS - 30, marquee_wait_ms(TIMING, LONG_TEXT, VIEW, PAUSE_MS + 530));
    CHECK_EQUAL(1u, marquee_wait_ms(TIMING, LONG_TEXT, VIEW, CYCLE_MS - 1));
    CHECK_EQUAL(PAUSE_MS + MARQUEE_STEP_MS, marquee_wait_ms(TIMING, LONG_TEXT, VIEW, CYCLE_MS));
}

static void drawing_marquees_keeps_the_soonest_step_until_the_next_frame(void)
{
    marquee_watch_start();
    CHECK_EQUAL(MARQUEE_STILL, marquee_watch_wait_ms());
    (void)marquee_offset(TIMING, LONG_TEXT, VIEW, 0);
    (void)marquee_offset(TIMING, LONG_TEXT, VIEW, PAUSE_MS + 530);
    (void)marquee_offset(TIMING, 50, VIEW, 0);
    CHECK_EQUAL(MARQUEE_STEP_MS - 30, marquee_watch_wait_ms());
    marquee_watch_start();
    CHECK_EQUAL(MARQUEE_STILL, marquee_watch_wait_ms());
}

static void a_zero_speed_never_scrolls(void)
{
    MarqueeTiming still = {.pause_ms = PAUSE_MS, .speed_px_per_s = 0, .gap_px = GAP};
    CHECK_EQUAL(0, marquee_offset(still, LONG_TEXT, VIEW, 5000));
}

static void very_wide_text_does_not_overflow_its_scroll_duration(void)
{
    MarqueeTiming timing = {.pause_ms = 0, .speed_px_per_s = 1000, .gap_px = 0};
    CHECK_EQUAL(705000, marquee_offset(timing, 5000000, 1, 705032));
}

static void text_and_gap_widths_are_added_without_signed_overflow(void)
{
    MarqueeTiming timing = {.pause_ms = 0, .speed_px_per_s = 1000, .gap_px = 1};
    CHECK_EQUAL(MARQUEE_STEP_MS, marquee_offset(timing, INT32_MAX, 1, MARQUEE_STEP_MS));
}

static void a_long_pause_and_short_scroll_never_make_a_zero_cycle(void)
{
    MarqueeTiming timing = {.pause_ms = UINT32_MAX, .speed_px_per_s = 2000, .gap_px = 0};
    CHECK_EQUAL(0, marquee_offset(timing, 2, 1, 1000));
}

static void scrolling_marquees_mark_the_rows_their_next_step_redraws(void)
{
    marquee_watch_start();
    CHECK_EQUAL(30,
                marquee_offset_in(TIMING, LONG_TEXT, (Rect){10, 40, VIEW, 20}, PAUSE_MS + 1000));
    marquee_offset_in(TIMING, LONG_TEXT, (Rect){0, 100, VIEW, 10}, 0);
    Rect band = marquee_watch_band();
    CHECK_EQUAL(40, band.y);
    CHECK_EQUAL(70, band.height);
}

static void text_that_fits_marks_no_rows_and_a_new_frame_forgets_them(void)
{
    marquee_watch_start();
    marquee_offset_in(TIMING, 50, (Rect){10, 40, VIEW, 20}, 5000);
    CHECK(rect_is_empty(marquee_watch_band()));
    marquee_offset_in(TIMING, LONG_TEXT, (Rect){10, 40, VIEW, 20}, 5000);
    CHECK(!rect_is_empty(marquee_watch_band()));
    marquee_watch_start();
    CHECK(rect_is_empty(marquee_watch_band()));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(text_that_fits_never_scrolls),
        TEST_CASE(scrolling_waits_for_the_pause),
        TEST_CASE(scrolling_moves_at_the_configured_speed),
        TEST_CASE(a_full_cycle_returns_to_the_start_and_pauses_again),
        TEST_CASE(the_offset_stays_within_one_text_and_gap),
        TEST_CASE(the_offset_moves_in_steps_of_the_marquee_pace),
        TEST_CASE(text_that_fits_never_asks_for_a_step),
        TEST_CASE(the_first_step_is_due_one_step_after_the_pause),
        TEST_CASE(a_moving_marquee_is_due_at_its_next_step_and_at_the_end_of_its_cycle),
        TEST_CASE(drawing_marquees_keeps_the_soonest_step_until_the_next_frame),
        TEST_CASE(a_zero_speed_never_scrolls),
        TEST_CASE(very_wide_text_does_not_overflow_its_scroll_duration),
        TEST_CASE(text_and_gap_widths_are_added_without_signed_overflow),
        TEST_CASE(a_long_pause_and_short_scroll_never_make_a_zero_cycle),
        TEST_CASE(scrolling_marquees_mark_the_rows_their_next_step_redraws),
        TEST_CASE(text_that_fits_marks_no_rows_and_a_new_frame_forgets_them),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
