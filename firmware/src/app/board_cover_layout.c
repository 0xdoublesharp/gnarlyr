#include "board_layout.h"

#include "cover_cache.h"
#include "gfx/text.h"

const BoardCoverLayout BOARD_COVER_LAYOUT = {
    .up_next_detail_max_width = 150,
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
