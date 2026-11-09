#include "board_layout.h"
#include "check.h"
#include "default_theme.h"
#include "hal/display.h"
#include "library/covers.h"

enum {
    MIN_VISIBLE_ROWS = 6,
    MIN_TEXT_COLUMN = 160,
    MAX_STRIP_PIXELS = 12000
};

static int line_height(const Font *font)
{
    return font->ascent + font->descent;
}

static void the_theme_matches_the_display(void)
{
    CHECK_EQUAL(HAL_DISPLAY_WIDTH, BOARD_THEME.metrics.screen_width);
    CHECK_EQUAL(HAL_DISPLAY_HEIGHT, BOARD_THEME.metrics.screen_height);
}

static void the_list_shows_at_least_six_rows(void)
{
    CHECK(theme_visible_rows(&BOARD_THEME) >= MIN_VISIBLE_ROWS);
}

static Theme theme_of_size(int size)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    return theme;
}

static void medium_text_is_the_board_theme(void)
{
    Theme theme = theme_of_size(TEXT_SIZE_MEDIUM);
    CHECK(theme.fonts.title == BOARD_THEME.fonts.title);
    CHECK(theme.fonts.body == BOARD_THEME.fonts.body);
    CHECK_EQUAL(BOARD_THEME.metrics.row_height, theme.metrics.row_height);
    CHECK_EQUAL(BOARD_THEME.metrics.header_height, theme.metrics.header_height);
    CHECK_EQUAL(TEXT_SIZE_MEDIUM, BOARD_THEME.metrics.text_size);
}

static void smaller_text_shows_more_rows(void)
{
    Theme small = theme_of_size(TEXT_SIZE_SMALL);
    Theme medium = theme_of_size(TEXT_SIZE_MEDIUM);
    Theme large = theme_of_size(TEXT_SIZE_LARGE);
    CHECK_EQUAL(8, theme_visible_rows(&small));
    CHECK_EQUAL(6, theme_visible_rows(&medium));
    CHECK_EQUAL(5, theme_visible_rows(&large));
}

static void larger_sizes_use_larger_fonts(void)
{
    for (int size = TEXT_SIZE_SMALL; size < TEXT_SIZE_LARGE; size++) {
        Theme smaller = theme_of_size(size);
        Theme larger = theme_of_size(size + 1);
        CHECK(line_height(smaller.fonts.body) < line_height(larger.fonts.body));
        CHECK(line_height(smaller.fonts.title) < line_height(larger.fonts.title));
    }
}

static void list_text_fits_its_row_at_every_size(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        CHECK(line_height(theme.fonts.body) <= theme.metrics.row_height);
        CHECK(line_height(theme.fonts.title) <= theme.metrics.row_height);
    }
}

static void the_title_fits_the_header_at_every_size(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        CHECK(line_height(theme.fonts.header) <= theme.metrics.header_height);
        CHECK(theme.fonts.header == BOARD_THEME.fonts.header);
    }
}

static void now_playing_text_lines_follow_each_other_without_overlap(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        const NowPlayingLayout *layout = board_now_playing_layout(&theme);
        CHECK(line_height(theme.fonts.title) <= layout->text_line_height);
        CHECK(line_height(theme.fonts.body) <= layout->text_line_height);
        CHECK(layout->title_top >= theme.metrics.header_height);
        CHECK(layout->title_top + layout->text_line_height <= layout->artist_top);
        CHECK(layout->artist_top + layout->text_line_height <= layout->album_top);
        CHECK(layout->album_top + layout->text_line_height <= layout->format_top);
        CHECK(layout->format_top + layout->small_line_height <= layout->detail_top);
        CHECK(layout->detail_top + layout->small_line_height <= layout->modes_top);
        CHECK(layout->modes_top + layout->small_line_height <= layout->progress_top);
    }
}

static void now_playing_keeps_the_progress_bar_and_times_on_screen(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        const NowPlayingLayout *layout = board_now_playing_layout(&theme);
        CHECK(layout->progress_top + layout->progress_height <= layout->times_top);
        CHECK(layout->times_top + layout->small_line_height <= HAL_DISPLAY_HEIGHT);
    }
}

static void now_playing_cover_sits_above_the_progress_bar(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        const NowPlayingLayout *layout = board_now_playing_layout(&theme);
        CHECK(layout->cover_top >= theme.metrics.header_height);
        CHECK(layout->cover_top + COVER_FULL_SIZE <= layout->progress_top);
    }
}

static void now_playing_leaves_a_text_column_beside_the_cover(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        int left =
            theme.metrics.padding + COVER_FULL_SIZE + board_now_playing_layout(&theme)->cover_gap;
        CHECK(HAL_DISPLAY_WIDTH - left - theme.metrics.padding >= MIN_TEXT_COLUMN);
    }
}

static void the_centre_cover_is_centred_on_screen(void)
{
    const CoverFlowLayout *layout = &BOARD_LAYOUT.cover_flow;
    CHECK_EQUAL(HAL_DISPLAY_WIDTH / 2, layout->center_x);
    CHECK(layout->center_top + layout->center_size <= HAL_DISPLAY_HEIGHT);
}

static void visible_side_covers_stay_on_screen(void)
{
    const CoverFlowLayout *layout = &BOARD_LAYOUT.cover_flow;
    int edge = layout->center_x + layout->side_offset + layout->side_spacing * 3 / 2 +
               layout->side_width / 2;
    CHECK(edge <= HAL_DISPLAY_WIDTH);
}

static void cover_flow_text_sits_below_the_cover_and_on_screen(void)
{
    const CoverFlowLayout *flow = &BOARD_LAYOUT.cover_flow;
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        const CoverFlowTextLayout *text = board_cover_flow_text_layout(&theme);
        CHECK(text->title_top >= flow->center_top + flow->center_size);
        CHECK(text->title_top + text->line_height <= text->artist_top);
        CHECK(text->artist_top + text->line_height <= HAL_DISPLAY_HEIGHT);
    }
}

static void cover_flow_text_lines_fit_the_theme_fonts(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        const CoverFlowTextLayout *text = board_cover_flow_text_layout(&theme);
        CHECK(line_height(theme.fonts.title) <= text->line_height);
        CHECK(line_height(theme.fonts.body) <= text->line_height);
    }
}

static void the_home_art_panel_holds_a_cover_and_leaves_room_for_the_list(void)
{
    CHECK(BOARD_LAYOUT.home_art_width >= COVER_FULL_SIZE);
    CHECK(BOARD_LAYOUT.home_art_width <= HAL_DISPLAY_WIDTH / 2);
}

static void up_next_details_leave_room_for_the_label(void)
{
    CHECK(BOARD_LAYOUT.up_next_detail_max_width <= HAL_DISPLAY_WIDTH / 2);
}

static void a_strip_buffer_stays_small_and_strips_cover_the_screen(void)
{
    CHECK(HAL_DISPLAY_WIDTH * BOARD_STRIP_HEIGHT <= MAX_STRIP_PIXELS);
    CHECK(BOARD_STRIP_HEIGHT <= HAL_DISPLAY_HEIGHT);
    CHECK(BOARD_STRIP_HEIGHT > 0);
}

static Theme portrait_theme_of_size(int size)
{
    Theme theme = theme_of_size(size);
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    return theme;
}

static void the_portrait_theme_stands_the_display_up(void)
{
    Theme theme = portrait_theme_of_size(TEXT_SIZE_MEDIUM);
    CHECK_EQUAL(HAL_DISPLAY_SHORT_SIDE, theme.metrics.screen_width);
    CHECK_EQUAL(HAL_DISPLAY_LONG_SIDE, theme.metrics.screen_height);
    CHECK(theme_is_portrait(&theme));
    CHECK(!theme_is_portrait(&BOARD_THEME));
    CHECK(theme.metrics.status_height > 0);
    CHECK_EQUAL(BOARD_THEME.metrics.header_height + theme.metrics.status_height,
                theme.metrics.header_height);
    CHECK_EQUAL(theme.metrics.padding, theme.metrics.header_inset);
}

static void turning_back_to_landscape_restores_the_board_theme(void)
{
    Theme theme = portrait_theme_of_size(TEXT_SIZE_LARGE);
    default_theme_set_orientation(&theme, ORIENTATION_LANDSCAPE);
    CHECK_EQUAL(BOARD_THEME.metrics.screen_width, theme.metrics.screen_width);
    CHECK_EQUAL(BOARD_THEME.metrics.screen_height, theme.metrics.screen_height);
    CHECK_EQUAL(BOARD_THEME.metrics.header_height, theme.metrics.header_height);
    CHECK_EQUAL(BOARD_THEME.metrics.header_inset, theme.metrics.header_inset);
    CHECK_EQUAL(0, theme.metrics.status_height);
    CHECK_EQUAL(TEXT_SIZE_LARGE, theme.metrics.text_size);
}

static void both_portraits_share_one_theme(void)
{
    Theme portrait = portrait_theme_of_size(TEXT_SIZE_SMALL);
    Theme flipped = theme_of_size(TEXT_SIZE_SMALL);
    default_theme_set_orientation(&flipped, ORIENTATION_PORTRAIT_FLIPPED);
    CHECK_EQUAL(portrait.metrics.screen_width, flipped.metrics.screen_width);
    CHECK_EQUAL(portrait.metrics.screen_height, flipped.metrics.screen_height);
    CHECK_EQUAL(portrait.metrics.header_height, flipped.metrics.header_height);
}

static void a_portrait_list_shows_thirteen_rows_at_most(void)
{
    Theme small = portrait_theme_of_size(TEXT_SIZE_SMALL);
    Theme medium = portrait_theme_of_size(TEXT_SIZE_MEDIUM);
    Theme large = portrait_theme_of_size(TEXT_SIZE_LARGE);
    CHECK_EQUAL(THEME_MAX_ROWS, theme_visible_rows(&small));
    CHECK_EQUAL(13, theme_visible_rows(&medium));
    CHECK_EQUAL(11, theme_visible_rows(&large));
}

static void the_portrait_title_fits_its_row_at_every_size(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = portrait_theme_of_size(size);
        CHECK(line_height(theme.fonts.header) <= theme_title_area(&theme).height);
    }
}

static void portrait_now_playing_lines_follow_each_other_on_screen(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = portrait_theme_of_size(size);
        const NowPlayingLayout *layout = board_now_playing_layout(&theme);
        CHECK(layout->cover_top >= theme.metrics.status_height);
        CHECK(layout->cover_top + COVER_FULL_SIZE <= layout->title_top);
        CHECK(line_height(theme.fonts.title) <= layout->text_line_height);
        CHECK(line_height(theme.fonts.body) <= layout->text_line_height);
        CHECK(layout->title_top + layout->text_line_height <= layout->artist_top);
        CHECK(layout->artist_top + layout->text_line_height <= layout->album_top);
        CHECK(layout->album_top + layout->text_line_height <= layout->format_top);
        CHECK(layout->format_top + layout->small_line_height <= layout->detail_top);
        CHECK(layout->detail_top + layout->small_line_height <= layout->modes_top);
        CHECK(layout->modes_top + layout->small_line_height <= layout->progress_top);
        CHECK(layout->progress_top + layout->progress_height <= layout->times_top);
        CHECK(layout->times_top + layout->small_line_height <= HAL_DISPLAY_LONG_SIDE);
    }
}

static void the_portrait_cover_stack_stays_on_screen_above_the_text(void)
{
    const CoverStackLayout *stack = &BOARD_LAYOUT.cover_stack;
    CHECK_EQUAL(HAL_DISPLAY_SHORT_SIDE / 2, stack->center_x);
    CHECK(stack->center_x + stack->side_width / 2 <= HAL_DISPLAY_SHORT_SIDE);
    int reach = stack->side_offset + stack->side_spacing * 3 / 2 + stack->side_height / 2;
    int middle = stack->center_top + stack->center_size / 2;
    CHECK(middle - reach >= 0);
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = portrait_theme_of_size(size);
        const CoverFlowTextLayout *text = board_cover_flow_text_layout(&theme);
        CHECK(middle + reach <= text->title_top);
        CHECK(line_height(theme.fonts.title) <= text->line_height);
        CHECK(line_height(theme.fonts.body) <= text->line_height);
        CHECK(text->title_top + text->line_height <= text->artist_top);
        CHECK(text->artist_top + text->line_height <= HAL_DISPLAY_LONG_SIDE);
    }
}

static void the_portrait_home_art_holds_a_cover_under_a_short_list(void)
{
    Theme theme = portrait_theme_of_size(TEXT_SIZE_LARGE);
    CHECK(BOARD_LAYOUT.home_art_min_height >= COVER_FULL_SIZE);
    CHECK(theme_content_area(&theme).height - BOARD_LAYOUT.home_art_min_height >=
          theme.metrics.row_height);
}

static void a_portrait_strip_buffer_holds_whole_rows(void)
{
    int pixels = HAL_DISPLAY_WIDTH * BOARD_STRIP_HEIGHT;
    CHECK(pixels / HAL_DISPLAY_SHORT_SIDE >= BOARD_STRIP_HEIGHT);
    CHECK(pixels / HAL_DISPLAY_SHORT_SIDE <= HAL_DISPLAY_LONG_SIDE);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_theme_matches_the_display),
        TEST_CASE(the_portrait_theme_stands_the_display_up),
        TEST_CASE(turning_back_to_landscape_restores_the_board_theme),
        TEST_CASE(both_portraits_share_one_theme),
        TEST_CASE(a_portrait_list_shows_thirteen_rows_at_most),
        TEST_CASE(the_portrait_title_fits_its_row_at_every_size),
        TEST_CASE(portrait_now_playing_lines_follow_each_other_on_screen),
        TEST_CASE(the_portrait_cover_stack_stays_on_screen_above_the_text),
        TEST_CASE(the_portrait_home_art_holds_a_cover_under_a_short_list),
        TEST_CASE(a_portrait_strip_buffer_holds_whole_rows),
        TEST_CASE(the_list_shows_at_least_six_rows),
        TEST_CASE(medium_text_is_the_board_theme),
        TEST_CASE(smaller_text_shows_more_rows),
        TEST_CASE(larger_sizes_use_larger_fonts),
        TEST_CASE(list_text_fits_its_row_at_every_size),
        TEST_CASE(the_title_fits_the_header_at_every_size),
        TEST_CASE(now_playing_text_lines_follow_each_other_without_overlap),
        TEST_CASE(now_playing_keeps_the_progress_bar_and_times_on_screen),
        TEST_CASE(now_playing_cover_sits_above_the_progress_bar),
        TEST_CASE(now_playing_leaves_a_text_column_beside_the_cover),
        TEST_CASE(the_centre_cover_is_centred_on_screen),
        TEST_CASE(visible_side_covers_stay_on_screen),
        TEST_CASE(cover_flow_text_sits_below_the_cover_and_on_screen),
        TEST_CASE(cover_flow_text_lines_fit_the_theme_fonts),
        TEST_CASE(the_home_art_panel_holds_a_cover_and_leaves_room_for_the_list),
        TEST_CASE(up_next_details_leave_room_for_the_label),
        TEST_CASE(a_strip_buffer_stays_small_and_strips_cover_the_screen),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
