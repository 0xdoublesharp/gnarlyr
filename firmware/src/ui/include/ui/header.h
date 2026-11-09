#pragma once

#include "gfx/canvas.h"
#include "ui/theme.h"

Rect header_title_bounds(const Theme *theme);
void header_draw(Canvas *canvas, const Theme *theme, const char *title);
