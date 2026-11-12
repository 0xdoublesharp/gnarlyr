#include "check.h"
#include "ui/toast.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    NOW_MS = 1000
};

static void a_new_toast_is_hidden(void)
{
    Toast toast = {0};
    CHECK(!toast.visible);
}

static void a_shown_toast_lasts_its_duration(void)
{
    Toast toast = {0};
    toast_show(&toast, "A", NOW_MS);
    toast_update(&toast, NOW_MS + TOAST_DURATION_MS - 1);
    CHECK(toast.visible);
    toast_update(&toast, NOW_MS + TOAST_DURATION_MS);
    CHECK(!toast.visible);
}

static void a_visible_toast_paints_its_box(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    Canvas canvas = ui_test_canvas();
    toast_draw(&toast, &canvas, &TEST_THEME);
    Rect area = toast_area(&toast, &TEST_THEME);
    CHECK(area.width > 0);
    CHECK(area.y + area.height <= TEST_SCREEN_HEIGHT);
    CHECK(ui_test_area_has_color(&canvas, area, TEST_THEME.colors.header_background));
    CHECK(ui_test_area_has_color(&canvas, area, TEST_THEME.colors.header_text));
}

static void a_toast_is_one_row_tall(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    CHECK_EQUAL(TEST_ROW_HEIGHT, toast_area(&toast, &TEST_THEME).height);
}

static void a_toast_too_long_for_one_line_wraps_and_grows_by_a_line(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB AB AB AB AB AB AB AB", NOW_MS);
    Rect area = toast_area(&toast, &TEST_THEME);
    int line = font_line_height(TEST_THEME.fonts.small);
    CHECK_EQUAL(TEST_ROW_HEIGHT + line, area.height);
    CHECK_EQUAL(TEST_SCREEN_WIDTH, area.width);
    CHECK(area.y >= 0 && area.y + area.height <= TEST_SCREEN_HEIGHT);
    Canvas canvas = ui_test_canvas();
    toast_draw(&toast, &canvas, &TEST_THEME);
    Rect upper = {area.x, area.y, area.width, area.height / 2};
    Rect lower = {area.x, area.y + area.height / 2, area.width, area.height - area.height / 2};
    CHECK(ui_test_area_has_color(&canvas, upper, TEST_THEME.colors.header_text));
    CHECK(ui_test_area_has_color(&canvas, lower, TEST_THEME.colors.header_text));
}

static void a_toast_never_grows_past_three_lines(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB",
               NOW_MS);
    int line = font_line_height(TEST_THEME.fonts.small);
    CHECK_EQUAL(TEST_ROW_HEIGHT + (TOAST_MAX_LINES - 1) * line,
                toast_area(&toast, &TEST_THEME).height);
}

static void a_toast_is_clipped_to_an_extremely_short_screen(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    Theme theme = TEST_THEME;
    theme.metrics.screen_height = 1;
    CHECK_EQUAL(1, toast_area(&toast, &theme).height);
}

static void a_toast_wider_than_the_screen_stays_on_screen(void)
{
    Toast toast = {0};
    toast_show(&toast, "ABABABABABABABABABABABABABABAB", NOW_MS);
    Rect area = toast_area(&toast, &TEST_THEME);
    CHECK(area.x >= 0);
    CHECK(area.x + area.width <= TEST_SCREEN_WIDTH);
}

static void a_hidden_toast_paints_nothing(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    toast_update(&toast, NOW_MS + TOAST_DURATION_MS);
    Canvas canvas = ui_test_canvas();
    Rect screen = {0, 0, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT};
    toast_draw(&toast, &canvas, &TEST_THEME);
    CHECK(!ui_test_area_has_color(&canvas, screen, TEST_THEME.colors.header_background));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_new_toast_is_hidden),
        TEST_CASE(a_shown_toast_lasts_its_duration),
        TEST_CASE(a_visible_toast_paints_its_box),
        TEST_CASE(a_toast_is_one_row_tall),
        TEST_CASE(a_hidden_toast_paints_nothing),
        TEST_CASE(a_toast_too_long_for_one_line_wraps_and_grows_by_a_line),
        TEST_CASE(a_toast_never_grows_past_three_lines),
        TEST_CASE(a_toast_is_clipped_to_an_extremely_short_screen),
        TEST_CASE(a_toast_wider_than_the_screen_stays_on_screen),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
