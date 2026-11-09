#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/theme.h"

void cassette_art_draw(Canvas *canvas, Rect area, const Theme *theme, uint32_t position_ms);
