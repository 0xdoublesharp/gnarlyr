#include "board_layout.h"

#include "cover_cache.h"

#ifdef ECHO_BOARD_ECHO
const BoardLayout BOARD_LAYOUT = {
    .home_art_width = 180,
    .up_next_detail_max_width = 150,
    .now_playing =
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
#else
const BoardLayout BOARD_LAYOUT = {
    .home_art_width = 144,
    .up_next_detail_max_width = 110,
    .now_playing =
        {
            .cover_top = 30,
            .cover_gap = 12,
            .title_top = 28,
            .artist_top = 50,
            .album_top = 70,
            .text_line_height = 22,
            .format_top = 97,
            .detail_top = 116,
            .modes_top = 131,
            .small_line_height = 16,
            .progress_top = 148,
            .progress_height = 3,
            .times_top = 153,
        },
    .cover_flow =
        {
            .center_x = 160,
            .center_top = 8,
            .center_size = COVER_FULL_SIZE,
            .side_offset = 86,
            .side_spacing = 30,
            .side_width = 46,
            .side_near_height = 104,
            .side_far_height = 78,
            .side_near_light = 230,
            .side_far_light = 140,
            .fade_start = 2 * COVER_FLOW_ONE,
            .fade_end = 2 * COVER_FLOW_ONE + COVER_FLOW_ONE / 2,
        },
    .cover_flow_text =
        {
            .title_top = 126,
            .artist_top = 148,
            .line_height = 20,
            .reflection_rows = 34,
        },
};
#endif
