#include "check.h"
#include "ui/header.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

static void the_header_is_filled_with_its_background(void)
{
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &TEST_THEME, "");
    Rect header = theme_header_area(&TEST_THEME);
    Rect body = {header.x, header.y, header.width, header.height - 1};
    CHECK(ui_test_area_has_only(&canvas, body, TEST_THEME.colors.header_background));
}

static void the_header_keeps_a_flat_surface_under_its_title(void)
{
    Theme theme = TEST_THEME;
    theme.colors.header_top = 0xFFFF;
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "");
    CHECK_EQUAL(theme.colors.header_background, canvas_pixel_at(&canvas, 0, 0));
    CHECK_EQUAL(theme.colors.header_background,
                canvas_pixel_at(&canvas, 0, TEST_HEADER_HEIGHT - 2));
}

static void a_divider_separates_the_header_from_the_content(void)
{
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &TEST_THEME, "");
    Rect divider = {0, TEST_HEADER_HEIGHT - 1, TEST_SCREEN_WIDTH, 1};
    CHECK(ui_test_area_has_only(&canvas, divider, TEST_THEME.colors.divider));
}

static void the_title_keeps_the_header_font_whatever_the_text_size(void)
{
    Theme theme = TEST_THEME;
    theme.fonts.title = NULL;
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "A");
    Rect header = theme_header_area(&theme);
    CHECK(ui_test_area_has_color(&canvas, header, theme.colors.header_text));
}

static void the_title_is_centred(void)
{
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &TEST_THEME, "A");
    Rect left_third = {0, 0, TEST_SCREEN_WIDTH / 3, TEST_HEADER_HEIGHT};
    Rect middle_third = {TEST_SCREEN_WIDTH / 3, 0, TEST_SCREEN_WIDTH / 3, TEST_HEADER_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, middle_third, TEST_THEME.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, left_third, TEST_THEME.colors.header_text));
}

static void a_long_title_stays_clear_of_the_header_insets(void)
{
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &TEST_THEME, "ABABABABABABAB");
    Rect left = {0, 0, TEST_HEADER_INSET, TEST_HEADER_HEIGHT};
    Rect right = {TEST_SCREEN_WIDTH - TEST_HEADER_INSET, 0, TEST_HEADER_INSET, TEST_HEADER_HEIGHT};
    CHECK(!ui_test_area_has_color(&canvas, left, TEST_THEME.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, right, TEST_THEME.colors.header_text));
}

static void the_title_sits_in_the_row_below_the_status_row(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.header_height = 2 * TEST_HEADER_HEIGHT;
    theme.metrics.status_height = TEST_HEADER_HEIGHT;
    theme.metrics.header_inset = theme.metrics.padding;
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "A");
    Rect status_row = {0, 0, TEST_SCREEN_WIDTH, TEST_HEADER_HEIGHT};
    Rect title_row = {0, TEST_HEADER_HEIGHT, TEST_SCREEN_WIDTH, TEST_HEADER_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, title_row, theme.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, status_row, theme.colors.header_text));
}

static void a_long_title_may_use_the_width_a_portrait_header_gives_it(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.header_inset = theme.metrics.padding;
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "ABABABABABABAB");
    Rect left = {0, 0, TEST_HEADER_INSET, TEST_HEADER_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, left, theme.colors.header_text));
}

static void a_theme_can_choose_an_accent_header_rule(void)
{
    Theme theme = TEST_THEME;
    theme.header_rule = true;
    theme.colors.accent = 0xF800;
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "");
    Rect rule = {0, TEST_HEADER_HEIGHT - 1, TEST_SCREEN_WIDTH, 1};
    CHECK(ui_test_area_has_only(&canvas, rule, theme.colors.accent));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_header_is_filled_with_its_background),
        TEST_CASE(a_theme_can_choose_an_accent_header_rule),
        TEST_CASE(the_header_keeps_a_flat_surface_under_its_title),
        TEST_CASE(a_divider_separates_the_header_from_the_content),
        TEST_CASE(the_title_is_centred),
        TEST_CASE(the_title_keeps_the_header_font_whatever_the_text_size),
        TEST_CASE(a_long_title_stays_clear_of_the_header_insets),
        TEST_CASE(the_title_sits_in_the_row_below_the_status_row),
        TEST_CASE(a_long_title_may_use_the_width_a_portrait_header_gives_it),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
