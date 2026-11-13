#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/fade.h"
#include "ui/theme.h"

enum {
    WIRE_BOOT_ONE = 256,
    WIRE_BOOT_OPEN_MS = 800,
    WIRE_BOOT_CLOSE_MS = 500
};

typedef struct WireBootFrame {
    const char *text;
    uint16_t progress;
    bool closing;
} WireBootFrame;

void wire_boot_draw(Canvas *canvas, const Theme *theme, Rect area, WireBootFrame frame);
void wire_boot_fade(Canvas *canvas, const Theme *theme, const Fade *fade, const char *text);
