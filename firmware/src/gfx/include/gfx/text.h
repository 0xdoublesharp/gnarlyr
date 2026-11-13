#pragma once

#include <stddef.h>

#include "gfx/canvas.h"
#include "gfx/font.h"

typedef struct TextStyle {
    const Font *font;
    Color color;
    bool tabular;
} TextStyle;

typedef struct TextSpan {
    const char *start;
    size_t byte_count;
} TextSpan;

int text_width(const Font *font, const char *text);
int text_digit_width(const Font *font);
int text_style_width(TextStyle style, const char *text);
int text_cell_width(TextStyle style, uint32_t codepoint, int advance);
int text_draw(Canvas *canvas, TextStyle style, Point pen, const char *text);
int text_draw_span(Canvas *canvas, TextStyle style, Point pen, TextSpan span);
void text_draw_glyph_scaled(Canvas *canvas, TextStyle style, Rect box, uint32_t codepoint,
                            int scale);
