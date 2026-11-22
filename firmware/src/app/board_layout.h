#pragma once

#include "settings/settings.h"
#include "ui/cover_flow.h"
#include "ui/theme.h"

enum {
    BOARD_STRIP_HEIGHT = 23,
    NOW_PLAYING_ART_COLUMN = 120,
    NOW_PLAYING_STAGE_WIDTH = 236,
    NOW_PLAYING_PORTRAIT_ART = 188,
    NOW_PLAYING_PORTRAIT_LARGE_ART = 150,
    NOW_PLAYING_COLUMN_GAP = 14
};

typedef struct NowPlayingFrame {
    Rect art;
    Rect column;
    Rect progress;
    Rect times;
    Rect cue;
} NowPlayingFrame;

typedef struct CoverFlowTextLayout {
    int title_top;
    int artist_top;
    int line_height;
    int reflection_rows;
} CoverFlowTextLayout;

typedef struct SplashStack {
    Rect mark;
    Rect label;
} SplashStack;

typedef struct BoardLayout {
    int home_art_width;
    int home_art_min_height;
} BoardLayout;

typedef struct BoardCoverLayout {
    int up_next_detail_max_width;
    CoverFlowLayout cover_flow;
    CoverFlowTextLayout cover_flow_text[TEXT_SIZE_COUNT];
    CoverStackLayout cover_stack;
    CoverFlowTextLayout cover_stack_text[TEXT_SIZE_COUNT];
} BoardCoverLayout;

extern const BoardLayout BOARD_LAYOUT;
extern const BoardCoverLayout BOARD_COVER_LAYOUT;

int board_text_size(const Theme *theme);
NowPlayingFrame board_now_playing_frame(const Theme *theme, bool stage, int lift);
const char *board_splash_text(const Theme *theme);
SplashStack board_splash_stack(const Theme *theme, Rect area);
const CoverFlowTextLayout *board_cover_flow_text_layout(const Theme *theme);
