#pragma once

#include "settings/settings.h"
#include "ui/cover_flow.h"
#include "ui/theme.h"

enum {
    BOARD_STRIP_HEIGHT = 23
};

typedef struct NowPlayingLayout {
    int cover_size;
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
    NowPlayingLayout now_playing[TEXT_SIZE_COUNT];
    CoverFlowLayout cover_flow;
    CoverFlowTextLayout cover_flow_text[TEXT_SIZE_COUNT];
    NowPlayingLayout now_playing_portrait[TEXT_SIZE_COUNT];
    CoverStackLayout cover_stack;
    CoverFlowTextLayout cover_stack_text[TEXT_SIZE_COUNT];
    int home_art_min_height;
} BoardLayout;

extern const BoardLayout BOARD_LAYOUT;

const NowPlayingLayout *board_now_playing_layout(const Theme *theme);
const char *board_splash_text(const Theme *theme);
const CoverFlowTextLayout *board_cover_flow_text_layout(const Theme *theme);
