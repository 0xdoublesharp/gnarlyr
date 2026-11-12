#include "home_art.h"

static void draw_horizon(Canvas *canvas, Rect area, const Theme *theme)
{
    if (!theme->horizon) {
        return;
    }
    int top = area.y + area.height / 2;
    int height = area.y + area.height - top;
    Color ink = color_between(theme->colors.divider, theme->colors.accent, 1, 3);
    for (int line = 0; line < 7; line++) {
        int y = top + line * line * height / 36;
        canvas_fill_rect(canvas, (Rect){area.x, y, area.width, 1}, ink);
    }
    for (int ray = -5; ray <= 5; ray++) {
        for (int y = 0; y < height; y++) {
            int x = area.x + area.width / 2 + ray * (6 + y * area.width / (height * 5));
            canvas_fill_rect(canvas, (Rect){x, top + y, 1, 1}, ink);
        }
    }
}

void home_art_draw_backdrop(Canvas *canvas, Rect area, const Theme *theme)
{
    Canvas clipped = canvas_clipped(canvas, area);
    canvas_fill_gradient(&clipped, area, theme->colors.backdrop_top, theme->colors.backdrop_bottom);
    draw_horizon(&clipped, area, theme);
}
