#include "board_layout.h"

#include "cover_cache.h"
#include "gfx/text.h"

const BoardCoverLayout BOARD_COVER_LAYOUT = {
    .cover_flow =
        {
            .center_x = 240,
            .center_top = 8,
            .center_size = 128,
            .side_offset = 112,
            .side_spacing = 52,
            .side_width = 60,
            .side_near_height = 120,
            .side_far_height = 90,
            .side_near_light = 230,
            .side_far_light = 140,
            .fade_start = 2 * COVER_FLOW_ONE,
            .fade_end = 2 * COVER_FLOW_ONE + COVER_FLOW_ONE / 2,
        },
    .cover_flow_text =
        {
            [TEXT_SIZE_SMALL] =
                {.title_top = 144, .artist_top = 170, .line_height = 24, .reflection_rows = 40},
            [TEXT_SIZE_MEDIUM] =
                {.title_top = 142, .artist_top = 172, .line_height = 28, .reflection_rows = 40},
            [TEXT_SIZE_LARGE] =
                {.title_top = 138, .artist_top = 174, .line_height = 35, .reflection_rows = 40},
        },
    .cover_stack =
        {
            .center_x = 111,
            .center_top = 130,
            .center_size = COVER_FULL_SIZE,
            .side_offset = 80,
            .side_spacing = 40,
            .side_width = 100,
            .side_height = 36,
        },
    .cover_stack_text =
        {
            [TEXT_SIZE_SMALL] = {.title_top = 374, .artist_top = 400, .line_height = 24},
            [TEXT_SIZE_MEDIUM] = {.title_top = 372, .artist_top = 402, .line_height = 28},
            [TEXT_SIZE_LARGE] = {.title_top = 368, .artist_top = 404, .line_height = 35},
        },
};

const CoverFlowTextLayout *board_cover_flow_text_layout(const Theme *theme)
{
    const CoverFlowTextLayout *table = theme_is_portrait(theme)
                                           ? BOARD_COVER_LAYOUT.cover_stack_text
                                           : BOARD_COVER_LAYOUT.cover_flow_text;
    return &table[board_text_size(theme)];
}

NowPlayingFrame board_now_playing_frame(const Theme *theme, bool stage, int lift)
{
    int padding = theme->metrics.padding;
    int wide = theme->metrics.screen_width;
    int high = theme->metrics.screen_height;
    int width = wide - 2 * padding;
    if (theme_is_portrait(theme)) {
        bool large = board_text_size(theme) == TEXT_SIZE_LARGE && !stage;
        int size = (large ? NOW_PLAYING_PORTRAIT_LARGE_ART : NOW_PLAYING_PORTRAIT_ART) - lift;
        int top = theme->metrics.header_height + padding + lift;
        Rect art =
            stage ? (Rect){padding, top, width, size} : (Rect){(wide - size) / 2, top, size, size};
        int column = top + size + NOW_PLAYING_COLUMN_GAP;
        return (NowPlayingFrame){art,
                                 {padding, column, width, high - 92 - column},
                                 {padding, high - 78, width, 3},
                                 {padding, high - 70, width, 20},
                                 {padding, high - 44, width, 44}};
    }
    int column = padding + (stage ? NOW_PLAYING_STAGE_WIDTH : NOW_PLAYING_ART_COLUMN) + padding;
    Rect art = stage ? (Rect){padding, 38, NOW_PLAYING_STAGE_WIDTH, 124}
                     : (Rect){padding + 4, 42, COVER_FULL_SIZE, COVER_FULL_SIZE};
    return (NowPlayingFrame){art,
                             {column, 38, wide - padding - column, high - 96},
                             {padding, high - 54, width, 3},
                             {padding, high - 48, width, 20},
                             {padding, high - 24, width, 24}};
}
