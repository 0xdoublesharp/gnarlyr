#include "board_layout.h"

#include "cover_cache.h"

const BoardLayout BOARD_LAYOUT = {
    .home_art_width = 180,
    .up_next_detail_max_width = 150,
    .now_playing =
        {
            .cover_top = 36,
            .cover_gap = 16,
            .title_top = 33,
            .artist_top = 61,
            .album_top = 89,
            .text_line_height = 28,
            .format_top = 120,
            .detail_top = 140,
            .modes_top = 158,
            .small_line_height = 18,
            .volume_top = 154,
            .deck_top = 178,
            .times_top = 181,
            .progress_top = 187,
            .progress_height = 6,
            .legend_top = 201,
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
            .title_top = 142,
            .artist_top = 172,
            .line_height = 28,
            .reflection_rows = 40,
        },
};
