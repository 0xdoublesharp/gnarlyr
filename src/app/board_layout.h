#pragma once

#include "ui/cover_flow.h"

enum {
    BOARD_STRIP_HEIGHT = 23
};

typedef struct NowPlayingLayout {
    int cover_top;
    int cover_gap;
    int title_top;
    int artist_top;
    int album_top;
    int text_line_height;
    int format_top;
    int detail_top;
    int modes_top;
    int small_line_height;
    int progress_top;
    int progress_height;
    int times_top;
} NowPlayingLayout;

typedef struct CoverFlowTextLayout {
    int title_top;
    int artist_top;
    int line_height;
    int reflection_rows;
} CoverFlowTextLayout;

typedef struct BoardLayout {
    int home_art_width;
    int up_next_detail_max_width;
    NowPlayingLayout now_playing;
    CoverFlowLayout cover_flow;
    CoverFlowTextLayout cover_flow_text;
} BoardLayout;

extern const BoardLayout BOARD_LAYOUT;
