#pragma once

#include "gfx/frame_diff.h"
#include "now_playing_screen.h"

enum {
    CASSETTE_NOW_PLAYING_FRAME_MS = 80
};

typedef struct CassetteNowPlayingLayout {
    Rect body;
    Rect label[3];
    Rect reels[2];
    Rect window;
} CassetteNowPlayingLayout;

CassetteNowPlayingLayout cassette_now_playing_layout(const Theme *theme);
int cassette_now_playing_radius(uint32_t position_ms, uint32_t duration_ms, int maximum);
void cassette_now_playing_draw(const NowPlayingScreen *screen, Canvas *canvas);
bool cassette_now_playing_animate(NowPlayingScreen *screen, FrameDiff *diff, bool visible);
