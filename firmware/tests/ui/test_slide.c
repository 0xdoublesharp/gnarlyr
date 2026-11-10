#include "check.h"
#include "ui/slide.h"

enum {
    WIDTH = 320,
    START_MS = 1000
};

static Slide started(SlideDirection direction)
{
    Slide slide;
    slide_start(&slide, START_MS, direction);
    return slide;
}

static void a_new_slide_has_not_moved(void)
{
    Slide slide = started(SLIDE_FORWARD);
    CHECK_EQUAL(0, slide_progress(&slide, START_MS, WIDTH));
    CHECK(!slide_finished(&slide, START_MS));
}

static void the_slide_eases_in_and_out_around_the_midpoint(void)
{
    Slide slide = started(SLIDE_FORWARD);
    int half = slide_progress(&slide, START_MS + SLIDE_DURATION_MS / 2, WIDTH);
    CHECK_EQUAL(WIDTH / 2, half);
    int quarter = slide_progress(&slide, START_MS + SLIDE_DURATION_MS / 4, WIDTH);
    int three_quarters = slide_progress(&slide, START_MS + SLIDE_DURATION_MS * 3 / 4, WIDTH);
    CHECK(quarter > 0 && quarter < WIDTH / 4);
    CHECK(three_quarters > WIDTH * 3 / 4 && three_quarters < WIDTH);
    CHECK(quarter < half - quarter);
    CHECK(WIDTH - three_quarters < three_quarters - half);
    CHECK_EQUAL(WIDTH, quarter + three_quarters);
}

static void the_slide_ends_at_the_full_width(void)
{
    Slide slide = started(SLIDE_BACK);
    CHECK_EQUAL(WIDTH, slide_progress(&slide, START_MS + SLIDE_DURATION_MS, WIDTH));
    CHECK_EQUAL(WIDTH, slide_progress(&slide, START_MS + 5 * SLIDE_DURATION_MS, WIDTH));
    CHECK(!slide_finished(&slide, START_MS + SLIDE_DURATION_MS - 1));
    CHECK(slide_finished(&slide, START_MS + SLIDE_DURATION_MS));
    CHECK(slide_finished(&slide, START_MS + SLIDE_DURATION_MS + 1));
}

static void forward_moves_the_old_screen_left_and_back_moves_it_right(void)
{
    Slide forward = started(SLIDE_FORWARD);
    Slide back = started(SLIDE_BACK);
    uint32_t half = START_MS + SLIDE_DURATION_MS / 2;
    CHECK_EQUAL(-WIDTH / 2, slide_leaving_x(&forward, half, WIDTH));
    CHECK_EQUAL(WIDTH / 2, slide_arriving_x(&forward, half, WIDTH));
    CHECK_EQUAL(WIDTH / 2, slide_leaving_x(&back, half, WIDTH));
    CHECK_EQUAL(-WIDTH / 2, slide_arriving_x(&back, half, WIDTH));
}

static void every_screen_width_moves_forward_without_overshooting(void)
{
    static const int widths[] = {0, 1, 2, 3, 222, 480};
    Slide slide = started(SLIDE_FORWARD);
    for (unsigned index = 0; index < sizeof widths / sizeof *widths; index++) {
        int previous = 0;
        for (uint32_t elapsed = 0; elapsed <= SLIDE_DURATION_MS + 16; elapsed++) {
            int progress = slide_progress(&slide, START_MS + elapsed, widths[index]);
            CHECK(progress >= previous && progress <= widths[index]);
            previous = progress;
        }
        CHECK_EQUAL(widths[index], previous);
    }
}

static void movement_is_symmetric_in_time_to_the_nearest_pixel(void)
{
    static const int widths[] = {1, 2, 3, 222, 480};
    Slide slide = started(SLIDE_FORWARD);
    for (unsigned index = 0; index < sizeof widths / sizeof *widths; index++) {
        for (uint32_t elapsed = 0; elapsed <= SLIDE_DURATION_MS; elapsed++) {
            int first = slide_progress(&slide, START_MS + elapsed, widths[index]);
            int last =
                slide_progress(&slide, START_MS + SLIDE_DURATION_MS - elapsed, widths[index]);
            int difference = first + last - widths[index];
            CHECK(difference >= -1 && difference <= 1);
        }
    }
}

static void reversing_a_small_screen_mirrors_both_edges(void)
{
    Slide forward = started(SLIDE_FORWARD);
    Slide back = started(SLIDE_BACK);
    for (int width = 1; width <= 3; width++) {
        for (uint32_t elapsed = 0; elapsed <= SLIDE_DURATION_MS; elapsed++) {
            uint32_t now_ms = START_MS + elapsed;
            CHECK_EQUAL(-slide_leaving_x(&forward, now_ms, width),
                        slide_leaving_x(&back, now_ms, width));
            CHECK_EQUAL(-slide_arriving_x(&forward, now_ms, width),
                        slide_arriving_x(&back, now_ms, width));
            CHECK_EQUAL(width, slide_arriving_x(&forward, now_ms, width) -
                                   slide_leaving_x(&forward, now_ms, width));
        }
    }
}

static void the_slide_finishes_across_the_clock_wrap(void)
{
    Slide slide;
    slide_start(&slide, UINT32_MAX - SLIDE_DURATION_MS / 2, SLIDE_BACK);
    CHECK_EQUAL(0, slide_progress(&slide, slide.start_ms, WIDTH));
    CHECK_EQUAL(WIDTH / 2, slide_progress(&slide, slide.start_ms + SLIDE_DURATION_MS / 2, WIDTH));
    CHECK_EQUAL(WIDTH, slide_progress(&slide, slide.start_ms + SLIDE_DURATION_MS, WIDTH));
    CHECK(slide_finished(&slide, slide.start_ms + SLIDE_DURATION_MS));
}

static void the_title_switches_halfway(void)
{
    Slide slide = started(SLIDE_FORWARD);
    CHECK(!slide_shows_arriving_title(&slide, START_MS + SLIDE_DURATION_MS / 2 - 1));
    CHECK(slide_shows_arriving_title(&slide, START_MS + SLIDE_DURATION_MS / 2));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_new_slide_has_not_moved),
        TEST_CASE(the_slide_eases_in_and_out_around_the_midpoint),
        TEST_CASE(the_slide_ends_at_the_full_width),
        TEST_CASE(forward_moves_the_old_screen_left_and_back_moves_it_right),
        TEST_CASE(the_title_switches_halfway),
        TEST_CASE(every_screen_width_moves_forward_without_overshooting),
        TEST_CASE(movement_is_symmetric_in_time_to_the_nearest_pixel),
        TEST_CASE(reversing_a_small_screen_mirrors_both_edges),
        TEST_CASE(the_slide_finishes_across_the_clock_wrap),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
