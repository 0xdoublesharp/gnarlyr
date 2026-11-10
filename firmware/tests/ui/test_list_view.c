#include "check.h"
#include "ui/list_view.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

typedef struct FakeItems {
    int count;
    const char *detail_at_one;
    bool opens_at_one;
} FakeItems;

static int fake_count(const void *context)
{
    const FakeItems *items = context;
    return items->count;
}

static const char *fake_label(const void *context, int index)
{
    (void)context;
    return index % 2 == 0 ? "A" : "B";
}

static const char *fake_detail(const void *context, int index)
{
    const FakeItems *items = context;
    return index == 1 ? items->detail_at_one : NULL;
}

static bool fake_opens(const void *context, int index)
{
    const FakeItems *items = context;
    return index == 1 && items->opens_at_one;
}

static FakeItems items;
static ListModel model;

static ListView list_of(int count)
{
    items = (FakeItems){.count = count, .detail_at_one = NULL};
    model = (ListModel){.context = &items,
                        .count = fake_count,
                        .label = fake_label,
                        .detail = fake_detail,
                        .opens = fake_opens};
    ListView view;
    list_view_init(&view, &model, &TEST_THEME, 0);
    return view;
}

static void move_times(ListView *view, int delta, int times)
{
    for (int step = 0; step < times; step++) {
        list_view_move(view, delta, false, 100);
    }
}

static Rect row_rect(int row)
{
    Rect content = theme_content_area(&TEST_THEME);
    return (Rect){0, content.y + row * TEST_ROW_HEIGHT, 1, TEST_ROW_HEIGHT};
}

static void a_new_list_selects_the_first_item(void)
{
    ListView view = list_of(10);
    CHECK_EQUAL(0, view.selected);
    CHECK_EQUAL(0, view.top);
    CHECK_EQUAL(TEST_VISIBLE_ROWS, view.visible_rows);
}

static void fitting_taller_rows_keeps_the_selection_on_screen(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 2);
    Theme taller = TEST_THEME;
    taller.metrics.row_height = 8;
    view.theme = &taller;
    list_view_fit(&view);
    CHECK_EQUAL(1, view.visible_rows);
    CHECK_EQUAL(2, view.selected);
    CHECK_EQUAL(2, view.top);
}

static void fitting_shorter_rows_fills_the_screen_to_the_last_item(void)
{
    ListView view = list_of(6);
    move_times(&view, 1, 5);
    CHECK_EQUAL(3, view.top);
    Theme shorter = TEST_THEME;
    shorter.metrics.row_height = 3;
    view.theme = &shorter;
    list_view_fit(&view);
    CHECK_EQUAL(5, view.visible_rows);
    CHECK_EQUAL(5, view.selected);
    CHECK_EQUAL(1, view.top);
}

static void moving_within_the_page_keeps_the_top(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 2);
    CHECK_EQUAL(2, view.selected);
    CHECK_EQUAL(0, view.top);
}

static void moving_past_the_last_visible_row_scrolls_down(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 3);
    CHECK_EQUAL(3, view.selected);
    CHECK_EQUAL(1, view.top);
}

static void moving_above_the_first_visible_row_scrolls_up(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 5);
    move_times(&view, -1, 3);
    CHECK_EQUAL(2, view.selected);
    CHECK_EQUAL(2, view.top);
}

static void wrapping_forward_from_the_last_item_returns_to_the_top(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 9);
    list_view_move(&view, 1, true, 100);
    CHECK_EQUAL(0, view.selected);
    CHECK_EQUAL(0, view.top);
}

static void moving_past_the_end_without_wrap_stays_on_the_last_item(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 12);
    CHECK_EQUAL(9, view.selected);
    CHECK_EQUAL(7, view.top);
}

static void wrapping_back_from_the_first_item_shows_the_last_page(void)
{
    ListView view = list_of(10);
    list_view_move(&view, -1, true, 100);
    CHECK_EQUAL(9, view.selected);
    CHECK_EQUAL(7, view.top);
}

static void a_short_list_never_scrolls(void)
{
    ListView view = list_of(2);
    list_view_move(&view, -1, true, 100);
    CHECK_EQUAL(1, view.selected);
    CHECK_EQUAL(0, view.top);
}

static void an_empty_list_ignores_moves(void)
{
    ListView view = list_of(0);
    list_view_move(&view, 1, true, 100);
    list_view_move(&view, -1, true, 100);
    CHECK_EQUAL(0, view.selected);
    CHECK_EQUAL(0, view.top);
}

static void the_marquee_restarts_only_when_the_selection_changes(void)
{
    ListView view = list_of(3);
    list_view_move(&view, 1, false, 500);
    CHECK_EQUAL(500, view.selection_changed_ms);
    move_times(&view, 1, 5);
    list_view_move(&view, 1, false, 900);
    CHECK_EQUAL(100, view.selection_changed_ms);
    list_view_restart_marquee(&view, 1200);
    CHECK_EQUAL(1200, view.selection_changed_ms);
}

static void selecting_an_index_scrolls_it_into_view(void)
{
    ListView view = list_of(10);
    list_view_select(&view, 8, 300);
    CHECK_EQUAL(8, view.selected);
    CHECK_EQUAL(6, view.top);
    CHECK_EQUAL(300, view.selection_changed_ms);
    list_view_select(&view, 1, 400);
    CHECK_EQUAL(1, view.top);
}

static void selecting_past_the_end_picks_the_last_item(void)
{
    ListView view = list_of(4);
    list_view_select(&view, 9, 300);
    CHECK_EQUAL(3, view.selected);
    list_view_select(&view, -2, 300);
    CHECK_EQUAL(0, view.selected);
}

static void the_selected_row_is_highlighted(void)
{
    ListView view = list_of(10);
    move_times(&view, 1, 1);
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 100);
    Rect selected = row_rect(1);
    selected.x = TEST_SCREEN_WIDTH / 2;
    CHECK(ui_test_area_has_only(&canvas, selected, TEST_THEME.colors.selection_background));
    CHECK(ui_test_area_has_only(&canvas, row_rect(0), TEST_THEME.colors.background));
    CHECK(ui_test_area_has_only(&canvas, row_rect(2), TEST_THEME.colors.background));
}

static void a_flat_focus_surface_has_two_accent_bracket_ends(void)
{
    Theme theme = TEST_THEME;
    theme.colors.selection_top = 0xFFFF;
    theme.colors.selection_shine = 0x07E0;
    theme.colors.accent = 0x07FF;
    theme.colors.accent_alt = 0xF81F;
    ListView view = list_of(10);
    view.theme = &theme;
    move_times(&view, 1, 1);
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&theme), 100);
    Rect row = row_rect(1);
    CHECK_EQUAL(theme.colors.accent, canvas_pixel_at(&canvas, 0, row.y));
    CHECK_EQUAL(theme.colors.accent_alt, canvas_pixel_at(&canvas, 0, row.y + row.height - 1));
    Rect flat = {TEST_SCREEN_WIDTH / 2, row.y, 1, row.height};
    CHECK(ui_test_area_has_only(&canvas, flat, theme.colors.selection_background));
}

static void the_scrollbar_thumb_sits_inset_without_a_track(void)
{
    Rect content = theme_content_area(&TEST_THEME);
    ListView long_list = list_of(10);
    Canvas canvas = ui_test_canvas();
    list_view_draw(&long_list, &canvas, content, 100);
    Rect edge = {TEST_SCREEN_WIDTH - TEST_THEME.metrics.scrollbar_inset, content.y,
                 TEST_THEME.metrics.scrollbar_inset, content.height};
    CHECK(ui_test_area_has_only(&canvas, edge, TEST_THEME.colors.background));
}

static void a_row_that_opens_something_shows_a_chevron(void)
{
    ListView view = list_of(3);
    items.opens_at_one = true;
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 100);
    Rect right_of_row_one = {TEST_SCREEN_WIDTH - 8, row_rect(1).y, 8, TEST_ROW_HEIGHT};
    Rect right_of_row_two = {TEST_SCREEN_WIDTH - 8, row_rect(2).y, 8, TEST_ROW_HEIGHT};
    CHECK(!ui_test_area_has_only(&canvas, right_of_row_one, TEST_THEME.colors.background));
    CHECK(ui_test_area_has_only(&canvas, right_of_row_two, TEST_THEME.colors.background));
}

static void the_scrollbar_appears_only_when_items_overflow(void)
{
    Rect content = theme_content_area(&TEST_THEME);
    int thumb_x = TEST_SCREEN_WIDTH - TEST_THEME.metrics.scrollbar_inset - 1;
    Rect right_edge = {thumb_x, content.y, 1, content.height};
    ListView long_list = list_of(10);
    Canvas canvas = ui_test_canvas();
    list_view_draw(&long_list, &canvas, content, 100);
    CHECK(ui_test_area_has_color(&canvas, right_edge, TEST_THEME.colors.scrollbar_thumb));
    ListView short_list = list_of(2);
    canvas = ui_test_canvas();
    list_view_draw(&short_list, &canvas, content, 100);
    CHECK(!ui_test_area_has_color(&canvas, right_edge, TEST_THEME.colors.scrollbar_thumb));
}

static void a_detail_is_drawn_in_the_secondary_color_at_the_right(void)
{
    ListView view = list_of(2);
    items.detail_at_one = "B";
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 100);
    Rect right_of_row_one = {TEST_SCREEN_WIDTH / 2, row_rect(1).y, TEST_SCREEN_WIDTH / 2,
                             TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, right_of_row_one, TEST_THEME.colors.text_secondary));
}

static void a_long_detail_stops_at_its_maximum_width(void)
{
    ListView view = list_of(2);
    items.detail_at_one = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    model.detail_max_width = TEST_SCREEN_WIDTH / 3;
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 100);
    Rect left_half_of_row_one = {0, row_rect(1).y, TEST_SCREEN_WIDTH / 2, TEST_ROW_HEIGHT};
    CHECK(!ui_test_area_has_color(&canvas, left_half_of_row_one, TEST_THEME.colors.text_secondary));
    Rect right_of_row_one = {TEST_SCREEN_WIDTH / 2, row_rect(1).y, TEST_SCREEN_WIDTH / 2,
                             TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, right_of_row_one, TEST_THEME.colors.text_secondary));
}

static void a_portrait_detail_leaves_the_label_its_room(void)
{
    static Theme theme;
    theme = TEST_THEME;
    theme.metrics.screen_width = TEST_SCREEN_HEIGHT;
    theme.metrics.screen_height = TEST_SCREEN_WIDTH;
    items = (FakeItems){.count = 2, .detail_at_one = "BBBBBBBBBB"};
    model = (ListModel){.context = &items,
                        .count = fake_count,
                        .label = fake_label,
                        .detail = fake_detail,
                        .opens = fake_opens};
    ListView view;
    list_view_init(&view, &model, &theme, 0);
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&theme), 100);
    Rect label = {theme.metrics.padding, row_rect(1).y, 3, TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, label, theme.colors.text));
    Rect detail = {theme.metrics.screen_width / 2, row_rect(1).y, theme.metrics.screen_width / 2,
                   TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, detail, theme.colors.text_secondary));
}

static IconId fake_icon(const void *context, int index)
{
    (void)context;
    (void)index;
    return ICON_MIXTAPE;
}

static void optional_row_icons_use_the_themes_accent_ink(void)
{
    ListView view = list_of(2);
    Theme theme = TEST_THEME;
    theme.colors.accent = 0xF800;
    view.theme = &theme;
    model.icon = fake_icon;
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&theme), 0);
    Rect icon = {theme.metrics.padding, row_rect(1).y, TEST_ROW_HEIGHT, TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, icon, theme.colors.accent));
}

static void selected_icons_use_only_the_selected_text_ink(void)
{
    ListView view = list_of(2);
    Theme theme = TEST_THEME;
    theme.colors.accent = 0xF800;
    view.theme = &theme;
    model.icon = fake_icon;
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&theme), 0);
    Rect icon = {theme.metrics.padding, row_rect(0).y, TEST_ROW_HEIGHT, TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_color(&canvas, icon, theme.colors.selection_text));
    CHECK(!ui_test_area_has_color(&canvas, icon, theme.colors.accent));
}

static void a_long_detail_cannot_paint_over_the_icon(void)
{
    ListView view = list_of(2);
    model.icon = fake_icon;
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 0);
    Color before[TEST_ROW_HEIGHT * TEST_ROW_HEIGHT];
    Rect icon = {TEST_THEME.metrics.padding, row_rect(1).y, TEST_ROW_HEIGHT, TEST_ROW_HEIGHT};
    for (int y = 0; y < icon.height; y++) {
        for (int x = 0; x < icon.width; x++) {
            before[y * icon.width + x] = canvas_pixel_at(&canvas, icon.x + x, icon.y + y);
        }
    }
    items.detail_at_one = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 0);
    for (int y = 0; y < icon.height; y++) {
        for (int x = 0; x < icon.width; x++) {
            CHECK_EQUAL(before[y * icon.width + x],
                        canvas_pixel_at(&canvas, icon.x + x, icon.y + y));
        }
    }
}

static const char *long_label(const void *context, int index)
{
    (void)context;
    (void)index;
    return "ABABABABABABABABABABABABABABABABABAB";
}

static void a_marquee_cannot_scroll_text_through_the_icon(void)
{
    ListView view = list_of(2);
    model.icon = fake_icon;
    model.label = long_label;
    Canvas canvas = ui_test_canvas();
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 0);
    Color before[TEST_ROW_HEIGHT * TEST_ROW_HEIGHT];
    Rect icon = {TEST_THEME.metrics.padding, row_rect(0).y, TEST_ROW_HEIGHT, TEST_ROW_HEIGHT};
    for (int y = 0; y < icon.height; y++) {
        for (int x = 0; x < icon.width; x++) {
            before[y * icon.width + x] = canvas_pixel_at(&canvas, icon.x + x, icon.y + y);
        }
    }
    list_view_draw(&view, &canvas, theme_content_area(&TEST_THEME), 5000);
    for (int y = 0; y < icon.height; y++) {
        for (int x = 0; x < icon.width; x++) {
            CHECK_EQUAL(before[y * icon.width + x],
                        canvas_pixel_at(&canvas, icon.x + x, icon.y + y));
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_new_list_selects_the_first_item),
        TEST_CASE(optional_row_icons_use_the_themes_accent_ink),
        TEST_CASE(selected_icons_use_only_the_selected_text_ink),
        TEST_CASE(a_long_detail_cannot_paint_over_the_icon),
        TEST_CASE(a_marquee_cannot_scroll_text_through_the_icon),
        TEST_CASE(fitting_taller_rows_keeps_the_selection_on_screen),
        TEST_CASE(fitting_shorter_rows_fills_the_screen_to_the_last_item),
        TEST_CASE(moving_within_the_page_keeps_the_top),
        TEST_CASE(moving_past_the_last_visible_row_scrolls_down),
        TEST_CASE(moving_above_the_first_visible_row_scrolls_up),
        TEST_CASE(wrapping_forward_from_the_last_item_returns_to_the_top),
        TEST_CASE(moving_past_the_end_without_wrap_stays_on_the_last_item),
        TEST_CASE(wrapping_back_from_the_first_item_shows_the_last_page),
        TEST_CASE(a_short_list_never_scrolls),
        TEST_CASE(an_empty_list_ignores_moves),
        TEST_CASE(the_marquee_restarts_only_when_the_selection_changes),
        TEST_CASE(selecting_an_index_scrolls_it_into_view),
        TEST_CASE(selecting_past_the_end_picks_the_last_item),
        TEST_CASE(the_selected_row_is_highlighted),
        TEST_CASE(the_scrollbar_appears_only_when_items_overflow),
        TEST_CASE(a_flat_focus_surface_has_two_accent_bracket_ends),
        TEST_CASE(the_scrollbar_thumb_sits_inset_without_a_track),
        TEST_CASE(a_row_that_opens_something_shows_a_chevron),
        TEST_CASE(a_detail_is_drawn_in_the_secondary_color_at_the_right),
        TEST_CASE(a_long_detail_stops_at_its_maximum_width),
        TEST_CASE(a_portrait_detail_leaves_the_label_its_room),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
