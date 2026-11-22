#include "check.h"
#include "ui/list_view.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

static ListRowRole roles[2];

static int two_rows(const void *context)
{
    (void)context;
    return 2;
}

static const char *letter(const void *context, int index)
{
    (void)context;
    (void)index;
    return "A";
}

static ListRowRole role_of(const void *context, int index)
{
    (void)context;
    return roles[index];
}

static IconId trash(const void *context, int index)
{
    (void)context;
    (void)index;
    return ICON_DELETE;
}

static ListModel model = {.count = two_rows, .label = letter, .role = role_of};

static Canvas draw_two(ListRowRole first, ListRowRole second, int selected)
{
    roles[0] = first;
    roles[1] = second;
    ListView view;
    list_view_init(&view, &model, &TEST_THEME, 0);
    list_view_select(&view, selected, 0);
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 0);
    return canvas;
}

static Rect row_rect(int row)
{
    return (Rect){0, TEST_HEADER_HEIGHT + row * TEST_ROW_HEIGHT, TEST_SCREEN_WIDTH,
                  TEST_ROW_HEIGHT};
}

static void a_danger_row_draws_its_label_in_the_danger_colour(void)
{
    Canvas canvas = draw_two(LIST_ROW_PLAIN, LIST_ROW_DANGER, 0);
    CHECK(ui_test_area_has_color(&canvas, row_rect(1), TEST_THEME.colors.danger));
    CHECK(!ui_test_area_has_color(&canvas, row_rect(0), TEST_THEME.colors.danger));
}

static void a_focused_danger_row_marks_its_bar_in_the_danger_colour(void)
{
    Canvas canvas = draw_two(LIST_ROW_PLAIN, LIST_ROW_DANGER, 1);
    Rect bar = {0, row_rect(1).y + 1, 1, TEST_ROW_HEIGHT - 2};
    CHECK(ui_test_area_has_only(&canvas, bar, TEST_THEME.colors.danger));
}

static void a_divided_row_draws_a_divider_along_its_top(void)
{
    Canvas canvas = draw_two(LIST_ROW_PLAIN, LIST_ROW_DANGER_DIVIDED, 0);
    Rect line = {0, row_rect(1).y, TEST_SCREEN_WIDTH - 4, 1};
    CHECK(ui_test_area_has_only(&canvas, line, TEST_THEME.colors.divider));
}

static void plain_rows_draw_no_divider(void)
{
    Canvas canvas = draw_two(LIST_ROW_PLAIN, LIST_ROW_PLAIN, 0);
    Rect line = {0, row_rect(1).y, TEST_SCREEN_WIDTH - 4, 1};
    CHECK(!ui_test_area_has_only(&canvas, line, TEST_THEME.colors.divider));
}

static void a_danger_row_draws_its_icon_in_the_danger_colour(void)
{
    model.icon = trash;
    Canvas canvas = draw_two(LIST_ROW_PLAIN, LIST_ROW_DANGER_DIVIDED, 1);
    model.icon = NULL;
    int x = TEST_THEME.metrics.padding;
    Rect plain = {x, row_rect(0).y, ICON_LARGE_GRID, TEST_ROW_HEIGHT};
    Rect danger = {x, row_rect(1).y + 1, ICON_LARGE_GRID, TEST_ROW_HEIGHT - 1};
    CHECK(ui_test_area_has_color(&canvas, danger, TEST_THEME.colors.danger));
    CHECK(!ui_test_area_has_color(&canvas, danger, TEST_THEME.colors.text_secondary));
    CHECK(ui_test_area_has_color(&canvas, plain, TEST_THEME.colors.text_secondary));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_danger_row_draws_its_label_in_the_danger_colour),
        TEST_CASE(a_focused_danger_row_marks_its_bar_in_the_danger_colour),
        TEST_CASE(a_divided_row_draws_a_divider_along_its_top),
        TEST_CASE(plain_rows_draw_no_divider),
        TEST_CASE(a_danger_row_draws_its_icon_in_the_danger_colour),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
