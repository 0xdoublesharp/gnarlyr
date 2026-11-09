#include "board_layout.h"

#include "cover_cache.h"

const BoardLayout BOARD_LAYOUT = {
    .home_art_width = 180,
    .up_next_detail_max_width = 150,
    .now_playing =
        {
            [TEXT_SIZE_SMALL] =
                {
                    .cover_top = 42,
                    .cover_gap = 16,
                    .title_top = 37,
                    .artist_top = 61,
                    .album_top = 85,
                    .text_line_height = 24,
                    .format_top = 114,
                    .detail_top = 136,
                    .modes_top = 154,
                    .small_line_height = 18,
                    .progress_top = 190,
                    .progress_height = 4,
                    .times_top = 197,
                },
            [TEXT_SIZE_MEDIUM] =
                {
                    .cover_top = 42,
                    .cover_gap = 16,
                    .title_top = 36,
                    .artist_top = 64,
                    .album_top = 92,
                    .text_line_height = 28,
                    .format_top = 124,
                    .detail_top = 146,
                    .modes_top = 164,
                    .small_line_height = 18,
                    .progress_top = 190,
                    .progress_height = 4,
                    .times_top = 197,
                },
            [TEXT_SIZE_LARGE] =
                {
                    .cover_top = 42,
                    .cover_gap = 16,
                    .title_top = 31,
                    .artist_top = 66,
                    .album_top = 101,
                    .text_line_height = 35,
                    .format_top = 137,
                    .detail_top = 157,
                    .modes_top = 175,
                    .small_line_height = 18,
                    .progress_top = 196,
                    .progress_height = 4,
                    .times_top = 202,
                },
        },
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
    .now_playing_portrait =
        {
            [TEXT_SIZE_SMALL] =
                {
                    .cover_top = 44,
                    .title_top = 174,
                    .artist_top = 198,
                    .album_top = 222,
                    .text_line_height = 24,
                    .format_top = 262,
                    .detail_top = 284,
                    .modes_top = 302,
                    .small_line_height = 18,
                    .progress_top = 440,
                    .progress_height = 4,
                    .times_top = 449,
                },
            [TEXT_SIZE_MEDIUM] =
                {
                    .cover_top = 44,
                    .title_top = 172,
                    .artist_top = 200,
                    .album_top = 228,
                    .text_line_height = 28,
                    .format_top = 268,
                    .detail_top = 290,
                    .modes_top = 308,
                    .small_line_height = 18,
                    .progress_top = 440,
                    .progress_height = 4,
                    .times_top = 449,
                },
            [TEXT_SIZE_LARGE] =
                {
                    .cover_top = 44,
                    .title_top = 168,
                    .artist_top = 203,
                    .album_top = 238,
                    .text_line_height = 35,
                    .format_top = 284,
                    .detail_top = 304,
                    .modes_top = 322,
                    .small_line_height = 18,
                    .progress_top = 440,
                    .progress_height = 4,
                    .times_top = 449,
                },
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
    .home_art_min_height = COVER_FULL_SIZE + 16,
};

static int text_size_of(const Theme *theme)
{
    int size = theme->metrics.text_size;
    return size >= 0 && size < TEXT_SIZE_COUNT ? size : TEXT_SIZE_MEDIUM;
}

const NowPlayingLayout *board_now_playing_layout(const Theme *theme)
{
    const NowPlayingLayout *table =
        theme_is_portrait(theme) ? BOARD_LAYOUT.now_playing_portrait : BOARD_LAYOUT.now_playing;
    return &table[text_size_of(theme)];
}

const CoverFlowTextLayout *board_cover_flow_text_layout(const Theme *theme)
{
    const CoverFlowTextLayout *table =
        theme_is_portrait(theme) ? BOARD_LAYOUT.cover_stack_text : BOARD_LAYOUT.cover_flow_text;
    return &table[text_size_of(theme)];
}
