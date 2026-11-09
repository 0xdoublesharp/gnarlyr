#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/theme.h"

enum {
    CASSETTE_WIDTH = 112,
    CASSETTE_HEIGHT = 72
};

typedef struct CassetteTape {
    uint32_t position_ms;
    uint32_t duration_ms;
    const char *label;
} CassetteTape;

int cassette_art_pack_size(uint32_t position_ms, uint32_t duration_ms);
void cassette_art_draw(Canvas *canvas, Rect area, const Theme *theme, CassetteTape tape);
