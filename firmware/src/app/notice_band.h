#pragma once

#include "gfx/canvas.h"
#include "ui/theme.h"

enum {
    NOTICE_BAND_LINES = 4
};

int notice_band_height(const Theme *theme, const char *text, int width);
void notice_band_draw(Canvas *canvas, const Theme *theme, Rect area, const char *text);
