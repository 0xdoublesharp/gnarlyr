#include "neon_art.h"

static int absolute(int value)
{
    return value < 0 ? -value : value;
}

NeonInk neon_art_ink(const Theme *theme, Color color)
{
    return (NeonInk){color, color_between(theme->colors.backdrop_top, color, 1, 4)};
}

static void line(Canvas *canvas, Point from, Point to, NeonInk ink)
{
    int dx = absolute(to.x - from.x);
    int dy = -absolute(to.y - from.y);
    int sx = from.x < to.x ? 1 : -1;
    int sy = from.y < to.y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        canvas_fill_rect(canvas, (Rect){from.x, from.y, 1, 1}, ink.bright);
        if (from.x == to.x && from.y == to.y) {
            return;
        }
        int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            from.x += sx;
        }
        if (twice <= dx) {
            error += dx;
            from.y += sy;
        }
    }
}

void neon_art_line(Canvas *canvas, Point from, Point to, NeonInk ink)
{
    for (int side = -1; side <= 1; side += 2) {
        line(canvas, (Point){from.x + side, from.y}, (Point){to.x + side, to.y},
             (NeonInk){ink.halo, ink.halo});
        line(canvas, (Point){from.x, from.y + side}, (Point){to.x, to.y + side},
             (NeonInk){ink.halo, ink.halo});
    }
    line(canvas, from, to, ink);
}

void neon_art_frame(Canvas *canvas, Rect area, NeonInk ink)
{
    int right = area.x + area.width - 1;
    int bottom = area.y + area.height - 1;
    neon_art_line(canvas, (Point){area.x, area.y}, (Point){right, area.y}, ink);
    neon_art_line(canvas, (Point){right, area.y}, (Point){right, bottom}, ink);
    neon_art_line(canvas, (Point){right, bottom}, (Point){area.x, bottom}, ink);
    neon_art_line(canvas, (Point){area.x, bottom}, (Point){area.x, area.y}, ink);
}
