#include "check.h"
#include "ui_test_theme.h"

static void the_header_spans_the_top_of_the_screen(void)
{
    Rect header = theme_header_area(&TEST_THEME);
    CHECK_EQUAL(0, header.y);
    CHECK_EQUAL(TEST_SCREEN_WIDTH, header.width);
    CHECK_EQUAL(TEST_HEADER_HEIGHT, header.height);
}

static void the_content_fills_the_rest_of_the_screen(void)
{
    Rect content = theme_content_area(&TEST_THEME);
    CHECK_EQUAL(TEST_HEADER_HEIGHT, content.y);
    CHECK_EQUAL(TEST_SCREEN_WIDTH, content.width);
    CHECK_EQUAL(TEST_SCREEN_HEIGHT - TEST_HEADER_HEIGHT, content.height);
}

static void visible_rows_are_the_whole_rows_that_fit_the_content(void)
{
    CHECK_EQUAL(TEST_VISIBLE_ROWS, theme_visible_rows(&TEST_THEME));
}

static Theme portrait_theme(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.screen_width = TEST_SCREEN_HEIGHT;
    theme.metrics.screen_height = TEST_SCREEN_WIDTH;
    return theme;
}

static void a_theme_taller_than_wide_is_portrait(void)
{
    Theme portrait = portrait_theme();
    CHECK(theme_is_portrait(&portrait));
    CHECK(!theme_is_portrait(&TEST_THEME));
}

static void without_a_status_row_the_title_row_is_the_whole_header(void)
{
    Rect title = theme_title_area(&TEST_THEME);
    CHECK_EQUAL(0, title.y);
    CHECK_EQUAL(TEST_SCREEN_WIDTH, title.width);
    CHECK_EQUAL(TEST_HEADER_HEIGHT, title.height);
}

static void a_status_row_sits_above_the_title_row(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.header_height = 2 * TEST_HEADER_HEIGHT;
    theme.metrics.status_height = TEST_HEADER_HEIGHT;
    Rect title = theme_title_area(&theme);
    CHECK_EQUAL(TEST_HEADER_HEIGHT, title.y);
    CHECK_EQUAL(TEST_HEADER_HEIGHT, title.height);
}

static void the_visible_rows_never_exceed_the_row_cache(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.screen_height = 1000;
    CHECK_EQUAL(THEME_MAX_ROWS, theme_visible_rows(&theme));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_header_spans_the_top_of_the_screen),
        TEST_CASE(the_content_fills_the_rest_of_the_screen),
        TEST_CASE(visible_rows_are_the_whole_rows_that_fit_the_content),
        TEST_CASE(a_theme_taller_than_wide_is_portrait),
        TEST_CASE(without_a_status_row_the_title_row_is_the_whole_header),
        TEST_CASE(a_status_row_sits_above_the_title_row),
        TEST_CASE(the_visible_rows_never_exceed_the_row_cache),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
