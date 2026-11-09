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

static void list_text_fits_its_row(void)
{
    CHECK(line_height(BOARD_THEME.fonts.body) <= BOARD_THEME.metrics.row_height);
    CHECK(line_height(BOARD_THEME.fonts.title) <= BOARD_THEME.metrics.row_height);
}

static void the_title_fits_the_header(void)
{
    CHECK(line_height(BOARD_THEME.fonts.title) <= BOARD_THEME.metrics.header_height);
}

static void now_playing_text_lines_follow_each_other_without_overlap(void)
{
    const NowPlayingLayout *layout = &BOARD_LAYOUT.now_playing;
    CHECK(layout->title_top + line_height(BOARD_THEME.fonts.title) <= layout->artist_top);
    CHECK(layout->artist_top + line_height(BOARD_THEME.fonts.body) <= layout->album_top);
    CHECK(layout->album_top + line_height(BOARD_THEME.fonts.body) <= layout->format_top);
    CHECK(layout->format_top < layout->detail_top);
    CHECK(layout->detail_top < layout->modes_top);
    CHECK(layout->modes_top + layout->small_line_height <= layout->progress_top);
}

static void now_playing_keeps_the_progress_bar_and_times_on_screen(void)
{
    const NowPlayingLayout *layout = &BOARD_LAYOUT.now_playing;
    CHECK(layout->progress_top + layout->progress_height <= layout->times_top);
    CHECK(layout->times_top + layout->small_line_height <= HAL_DISPLAY_HEIGHT);
}

static void now_playing_cover_sits_above_the_progress_bar(void)
{
    const NowPlayingLayout *layout = &BOARD_LAYOUT.now_playing;
    CHECK(layout->cover_top >= BOARD_THEME.metrics.header_height);
    CHECK(layout->cover_top + COVER_FULL_SIZE <= layout->progress_top);
}

static void now_playing_leaves_a_text_column_beside_the_cover(void)
{
    int left = BOARD_THEME.metrics.padding + COVER_FULL_SIZE + BOARD_LAYOUT.now_playing.cover_gap;
    int column = HAL_DISPLAY_WIDTH - left - BOARD_THEME.metrics.padding;
    CHECK(column >= MIN_TEXT_COLUMN);
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
    const CoverFlowTextLayout *text = &BOARD_LAYOUT.cover_flow_text;
    CHECK(text->title_top >= flow->center_top + flow->center_size);
    CHECK(text->title_top + text->line_height <= text->artist_top);
    CHECK(text->artist_top + text->line_height <= HAL_DISPLAY_HEIGHT);
}

static void cover_flow_text_lines_fit_the_theme_fonts(void)
{
    const CoverFlowTextLayout *text = &BOARD_LAYOUT.cover_flow_text;
    CHECK(line_height(BOARD_THEME.fonts.title) <= text->line_height);
    CHECK(line_height(BOARD_THEME.fonts.body) <= text->line_height);
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

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_theme_matches_the_display),
        TEST_CASE(the_list_shows_at_least_six_rows),
        TEST_CASE(list_text_fits_its_row),
        TEST_CASE(the_title_fits_the_header),
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
