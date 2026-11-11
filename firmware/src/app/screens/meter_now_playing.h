#pragma once

#include "now_playing_screen.h"

typedef struct MeterNowPlayingLayout {
    Rect stage;
    Rect dials[2];
    Rect spectrum;
    Rect text[2];
} MeterNowPlayingLayout;

MeterNowPlayingLayout meter_now_playing_layout(const Theme *theme);
void meter_now_playing_reset(void);
void meter_now_playing_draw(const NowPlayingScreen *screen, Canvas *canvas);
bool meter_now_playing_animate(NowPlayingScreen *screen, FrameDiff *diff, bool visible);
