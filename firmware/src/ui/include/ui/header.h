#pragma once

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "ui/theme.h"

Rect header_title_bounds(const Theme *theme);
Rect header_title_area(const Theme *theme, const Font *font, const char *title);
void header_draw(Canvas *canvas, const Theme *theme, const char *title);
