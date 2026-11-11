#pragma once

#include "core/audio_visual.h"
#include "gfx/canvas.h"
#include "gfx/strip_renderer.h"
#include "ui/theme.h"

typedef struct NeonInk {
    Color bright;
    Color halo;
} NeonInk;

NeonInk neon_art_ink(const Theme *theme, Color color);
void neon_art_line(Canvas *canvas, Point from, Point to, NeonInk ink);
void neon_art_frame(Canvas *canvas, Rect area, NeonInk ink);

void neon_art_render(Rect area, Color background, FrameDrawer drawer, FrameSink sink);

AudioVisualWork *neon_art_audio_work(void);
