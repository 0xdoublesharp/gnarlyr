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

static void a_published_room_puts_the_title_at_its_left_edge(void)
{
    Theme theme = TEST_THEME;
    theme.header_room = (HeaderRoom){10, 30, 0};
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "A");
    Rect before = {0, 0, 10, TEST_HEADER_HEIGHT};
    Rect start = {10, 0, 4, TEST_HEADER_HEIGHT};
    CHECK(!ui_test_area_has_color(&canvas, before, theme.colors.header_text));
    CHECK(ui_test_area_has_color(&canvas, start, theme.colors.header_text));
}

static void a_title_wider_than_its_room_stays_inside_it(void)
{
    Theme theme = TEST_THEME;
    theme.header_room = (HeaderRoom){10, 30, 0};
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "ABABABABABABAB");
    Rect after = {30, 0, TEST_SCREEN_WIDTH - 30, TEST_HEADER_HEIGHT};
    CHECK(!ui_test_area_has_color(&canvas, after, theme.colors.header_text));
    theme.header_room.shift = 3;
    header_draw(&canvas, &theme, "ABABABABABABAB");
    CHECK(!ui_test_area_has_color(&canvas, after, theme.colors.header_text));
}

static void a_marquee_shift_moves_the_title_left(void)
{
    Theme theme = TEST_THEME;
    theme.header_room = (HeaderRoom){10, 30, 0};
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, &theme, "ABABABABABABAB");
    Color still[TEST_HEADER_HEIGHT][20];
    for (int y = 0; y < TEST_HEADER_HEIGHT; y++) {
        for (int x = 0; x < 20; x++) {
            still[y][x] = canvas_pixel_at(&canvas, 10 + x, y);
        }
    }
    bool moved = false;
    for (int shift = 1; shift < 6 && !moved; shift++) {
        theme.header_room.shift = (int16_t)shift;
        header_draw(&canvas, &theme, "ABABABABABABAB");
        for (int y = 0; y < TEST_HEADER_HEIGHT; y++) {
            for (int x = 0; x < 20; x++) {
                moved |= canvas_pixel_at(&canvas, 10 + x, y) != still[y][x];
            }
        }
    }
    CHECK(moved);
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
        TEST_CASE(a_published_room_puts_the_title_at_its_left_edge),
        TEST_CASE(a_title_wider_than_its_room_stays_inside_it),
        TEST_CASE(a_marquee_shift_moves_the_title_left),
        TEST_CASE(a_long_title_may_use_the_width_a_portrait_header_gives_it),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
