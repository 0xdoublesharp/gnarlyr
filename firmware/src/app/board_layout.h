#pragma once

#include "settings/settings.h"
#include "ui/cover_flow.h"
#include "ui/theme.h"

enum {
    BOARD_STRIP_HEIGHT = 23
};

typedef struct NowPlayingLayout {
    int16_t cover_size;
    int16_t cover_top;
    int16_t cover_gap;
    int16_t title_top;
    int16_t artist_top;
    int16_t album_top;
    int16_t text_line_height;
    int16_t format_top;
    int16_t detail_top;
    int16_t modes_top;
    int16_t small_line_height;
    int16_t progress_top;
    int16_t progress_height;
    int16_t times_top;
} NowPlayingLayout;

typedef struct CoverFlowTextLayout {
    int title_top;
    int artist_top;
    int line_height;
    int reflection_rows;
} CoverFlowTextLayout;

typedef struct BoardLayout {
    int home_art_width;
    int home_art_min_height;
} BoardLayout;

typedef struct BoardCoverLayout {
    int up_next_detail_max_width;
    NowPlayingLayout now_playing[TEXT_SIZE_COUNT];
    NowPlayingLayout now_playing_portrait[TEXT_SIZE_COUNT];
    CoverFlowLayout cover_flow;
    CoverFlowTextLayout cover_flow_text[TEXT_SIZE_COUNT];
    CoverStackLayout cover_stack;
    CoverFlowTextLayout cover_stack_text[TEXT_SIZE_COUNT];
} BoardCoverLayout;

extern const BoardLayout BOARD_LAYOUT;
extern const BoardCoverLayout BOARD_COVER_LAYOUT;

int board_text_size(const Theme *theme);
const NowPlayingLayout *board_now_playing_layout(const Theme *theme);
const char *board_splash_text(const Theme *theme);
const CoverFlowTextLayout *board_cover_flow_text_layout(const Theme *theme);
