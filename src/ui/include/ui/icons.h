#pragma once

#include <stdbool.h>

#include "gfx/canvas.h"

void icon_draw_play(Canvas *canvas, Rect area, Color color);
void icon_draw_pause(Canvas *canvas, Rect area, Color color);
void icon_draw_stop(Canvas *canvas, Rect area, Color color);
void icon_draw_next(Canvas *canvas, Rect area, Color color);
void icon_draw_previous(Canvas *canvas, Rect area, Color color);
void icon_draw_bolt(Canvas *canvas, Rect area, Color color);
void icon_draw_bluetooth(Canvas *canvas, Rect area, Color color);
void icon_draw_lock(Canvas *canvas, Rect area, Color color);
int icon_shuffle_width(bool albums);
void icon_draw_shuffle(Canvas *canvas, Rect area, Color color, bool albums);
