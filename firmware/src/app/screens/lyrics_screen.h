#pragma once

#include "lyrics_lend.h"
#include "player.h"
#include "ui/screen.h"
#include "ui/theme.h"

typedef struct LyricsScreenConfig {
    Player *player;
    const Library *library;
    const Theme *theme;
} LyricsScreenConfig;

typedef struct LyricsScreen {
    Screen screen;
    LyricsScreenConfig config;
    char path[PLAYER_PATH_CAPACITY];
    uint32_t cue_start_ms;
    uint32_t token;
    uint16_t track;
    int16_t scroll;
    bool known;
} LyricsScreen;

Screen *lyrics_screen_init(LyricsScreen *screen, LyricsScreenConfig config);
