#include <string.h>

#include "about_layout.h"
#include "board_layout.h"
#include "check.h"
#include "default_theme.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "library/covers.h"
#include "ui/header.h"

enum {
    MIN_VISIBLE_ROWS = 6,
    MIN_TEXT_COLUMN = 160,
    MAX_STRIP_PIXELS = 12000,
    HORIZON_SHARE = 9,
    HORIZON_PARTS = 16,
    MIN_SPLASH_MARK = 48,
    MIN_ABOUT_ROWS = 3
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

static void the_centre_cover_is_centred_on_screen(void)
{
    const CoverFlowLayout *layout = &BOARD_COVER_LAYOUT.cover_flow;
    CHECK_EQUAL(HAL_DISPLAY_WIDTH / 2, layout->center_x);
    CHECK(layout->center_top + layout->center_size <= HAL_DISPLAY_HEIGHT);
}

static void visible_side_covers_stay_on_screen(void)
{
    const CoverFlowLayout *layout = &BOARD_COVER_LAYOUT.cover_flow;
    int edge = layout->center_x + layout->side_offset + layout->side_spacing * 3 / 2 +
               layout->side_width / 2;
    CHECK(edge <= HAL_DISPLAY_WIDTH);
}

static void cover_flow_text_sits_below_the_cover_and_on_screen(void)
{
    const CoverFlowLayout *flow = &BOARD_COVER_LAYOUT.cover_flow;
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
    CHECK(BOARD_COVER_LAYOUT.up_next_detail_max_width <= HAL_DISPLAY_WIDTH / 2);
}

static void a_strip_buffer_stays_small_and_strips_cover_the_screen(void)
{
    CHECK(HAL_DISPLAY_WIDTH * BOARD_STRIP_HEIGHT <= MAX_STRIP_PIXELS);
    CHECK(BOARD_STRIP_HEIGHT <= HAL_DISPLAY_HEIGHT);
    CHECK(BOARD_STRIP_HEIGHT > 0);
}

enum {
    PORTRAIT_COVER_FRAME = 4,
    PORTRAIT_COVER_SHADOW = 5
};

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
    CHECK_EQUAL(BOARD_THEME.metrics.header_height, theme.metrics.header_height);
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
        CHECK(line_height(theme.fonts.header) <= theme_header_area(&theme).height);
    }
}

static void check_frame(const Theme *theme, NowPlayingFrame frame, bool stage)
{
    int height = theme->metrics.screen_height;
    int frame_width = stage ? 0 : PORTRAIT_COVER_FRAME;
    CHECK(frame.art.y >= theme->metrics.header_height);
    CHECK(frame.column.y >= theme->metrics.header_height);
    CHECK(frame.art.y + frame.art.height + frame_width <= frame.progress.y - 3);
    CHECK(frame.column.y + frame.column.height <= frame.progress.y - 3);
    CHECK(frame.progress.y + frame.progress.height + 2 <= frame.times.y);
    CHECK(frame.times.y + line_height(theme->fonts.small) <= frame.cue.y);
    CHECK(frame.cue.y + frame.cue.height <= height);
    CHECK(frame.column.x + frame.column.width <=
          theme->metrics.screen_width - theme->metrics.padding);
}

static void now_playing_keeps_art_text_transport_and_cue_apart(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme themes[] = {theme_of_size(size), portrait_theme_of_size(size)};
        for (int index = 0; index < 2; index++) {
            for (int stage = 0; stage < 2; stage++) {
                check_frame(&themes[index], board_now_playing_frame(&themes[index], stage, 0),
                            stage);
            }
        }
    }
}

static void now_playing_leaves_a_text_column_beside_the_art(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_of_size(size);
        for (int stage = 0; stage < 2; stage++) {
            NowPlayingFrame frame = board_now_playing_frame(&theme, stage, 0);
            CHECK(frame.column.x >= frame.art.x + frame.art.width + theme.metrics.padding);
            CHECK(frame.column.width >= MIN_TEXT_COLUMN);
            CHECK(frame.column.height >= line_height(theme.fonts.title) +
                                             line_height(theme.fonts.body) +
                                             line_height(theme.fonts.small));
        }
    }
}

static void now_playing_puts_the_text_under_the_art_in_portrait(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = portrait_theme_of_size(size);
        NowPlayingFrame frame = board_now_playing_frame(&theme, false, 0);
        CHECK_EQUAL(theme.metrics.header_height + theme.metrics.padding, frame.art.y);
        CHECK(frame.art.y + frame.art.height < frame.column.y);
        int margin = (theme.metrics.screen_width - frame.art.width) / 2 - PORTRAIT_COVER_FRAME;
        CHECK(margin >= theme.metrics.padding);
        CHECK(frame.art.x + frame.art.width + PORTRAIT_COVER_SHADOW <=
              theme.metrics.screen_width - theme.metrics.padding);
        CHECK(frame.column.height >= 2 * line_height(theme.fonts.title) +
                                         line_height(theme.fonts.body) +
                                         2 * line_height(theme.fonts.small));
    }
}

static void a_title_in_the_portrait_body_lowers_and_shrinks_the_art(void)
{
    Theme theme = portrait_theme_of_size(TEXT_SIZE_MEDIUM);
    NowPlayingFrame plain = board_now_playing_frame(&theme, false, 0);
    NowPlayingFrame lifted = board_now_playing_frame(&theme, false, 27);
    CHECK_EQUAL(plain.art.y + 27, lifted.art.y);
    CHECK_EQUAL(plain.art.height - 27, lifted.art.height);
    CHECK_EQUAL(plain.column.y, lifted.column.y);
    check_frame(&theme, lifted, false);
}

static void the_portrait_cover_stack_stays_on_screen_above_the_text(void)
{
    const CoverStackLayout *stack = &BOARD_COVER_LAYOUT.cover_stack;
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

static Theme theme_turned(int size, Orientation orientation)
{
    Theme theme = theme_of_size(size);
    default_theme_set_orientation(&theme, orientation);
    return theme;
}

static void the_home_title_fits_the_header_at_every_size_and_orientation(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            Theme theme = theme_turned(size, (Orientation)orientation);
            CHECK(text_width(theme.fonts.header, FIRMWARE_SHORT_NAME) <=
                  header_title_bounds(&theme).width);
        }
    }
}

static void the_splash_shows_the_full_name_at_every_size_in_landscape(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_turned(size, ORIENTATION_LANDSCAPE);
        CHECK(strcmp(FIRMWARE_NAME, board_splash_text(&theme)) == 0);
    }
}

static void the_portrait_splash_shows_the_short_name_only_where_the_full_name_is_too_wide(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_turned(size, ORIENTATION_PORTRAIT);
        bool fits = text_width(theme.fonts.title, FIRMWARE_NAME) <= theme.metrics.screen_width;
        CHECK(strcmp(fits ? FIRMWARE_NAME : FIRMWARE_SHORT_NAME, board_splash_text(&theme)) == 0);
    }
}

static void the_splash_text_fits_the_screen_at_every_size_and_orientation(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            Theme theme = theme_turned(size, (Orientation)orientation);
            CHECK(text_width(theme.fonts.title, board_splash_text(&theme)) <=
                  theme.metrics.screen_width);
        }
    }
}

static bool inside(Rect inner, Rect outer)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

static void check_stack(const Theme *theme, Rect area)
{
    SplashStack stack = board_splash_stack(theme, area);
    CHECK(inside(stack.mark, area));
    CHECK(inside(stack.label, area));
    CHECK(stack.mark.y + stack.mark.height <= stack.label.y);
    CHECK(stack.mark.width >= MIN_SPLASH_MARK);
    CHECK_EQUAL(stack.mark.width, stack.mark.height);
    int offset = 2 * stack.mark.x + stack.mark.width - 2 * area.x - area.width;
    CHECK(offset >= -1 && offset <= 1);
    CHECK(text_width(theme->fonts.title, board_splash_text(theme)) <= stack.label.width);
}

static void the_splash_stacks_the_mark_over_the_name_in_the_screen_and_the_sky(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            Theme theme = theme_turned(size, (Orientation)orientation);
            int width = theme.metrics.screen_width;
            int height = theme.metrics.screen_height;
            check_stack(&theme, (Rect){0, 0, width, height});
            check_stack(&theme, (Rect){0, 0, width, height * HORIZON_SHARE / HORIZON_PARTS});
        }
    }
}

static void about_puts_the_mark_and_name_in_a_band_above_the_rows(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            Theme theme = theme_turned(size, (Orientation)orientation);
            AboutBand about = about_layout_band(&theme, FIRMWARE_NAME);
            Rect content = theme_content_area(&theme);
            CHECK_EQUAL(content.y, about.band.y);
            CHECK(inside(about.mark, about.band));
            CHECK(inside(about.name, about.band));
            CHECK(about.mark.x + about.mark.width < about.name.x);
            CHECK(text_width(theme.fonts.title, FIRMWARE_NAME) <= about.name.width);
            CHECK(content.height - about.band.height >= MIN_ABOUT_ROWS * theme.metrics.row_height);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_theme_matches_the_display),
        TEST_CASE(the_home_title_fits_the_header_at_every_size_and_orientation),
        TEST_CASE(the_splash_shows_the_full_name_at_every_size_in_landscape),
        TEST_CASE(the_portrait_splash_shows_the_short_name_only_where_the_full_name_is_too_wide),
        TEST_CASE(the_splash_text_fits_the_screen_at_every_size_and_orientation),
        TEST_CASE(the_splash_stacks_the_mark_over_the_name_in_the_screen_and_the_sky),
        TEST_CASE(about_puts_the_mark_and_name_in_a_band_above_the_rows),
        TEST_CASE(the_portrait_theme_stands_the_display_up),
        TEST_CASE(turning_back_to_landscape_restores_the_board_theme),
        TEST_CASE(both_portraits_share_one_theme),
        TEST_CASE(a_portrait_list_shows_thirteen_rows_at_most),
        TEST_CASE(the_portrait_title_fits_its_row_at_every_size),
        TEST_CASE(the_portrait_cover_stack_stays_on_screen_above_the_text),
        TEST_CASE(the_portrait_home_art_holds_a_cover_under_a_short_list),
        TEST_CASE(a_portrait_strip_buffer_holds_whole_rows),
        TEST_CASE(the_list_shows_at_least_six_rows),
        TEST_CASE(medium_text_is_the_board_theme),
        TEST_CASE(smaller_text_shows_more_rows),
        TEST_CASE(larger_sizes_use_larger_fonts),
        TEST_CASE(list_text_fits_its_row_at_every_size),
        TEST_CASE(the_title_fits_the_header_at_every_size),
        TEST_CASE(now_playing_keeps_art_text_transport_and_cue_apart),
        TEST_CASE(now_playing_leaves_a_text_column_beside_the_art),
        TEST_CASE(now_playing_puts_the_text_under_the_art_in_portrait),
        TEST_CASE(a_title_in_the_portrait_body_lowers_and_shrinks_the_art),
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
