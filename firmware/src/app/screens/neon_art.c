#include "neon_art.h"

#include "playing_work.h"

AudioVisualWork *neon_art_audio_work(void)
{
    return &playing_work_styles()->neon.audio;
}

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
    Rect bounds = {from.x < to.x ? from.x : to.x, from.y < to.y ? from.y : to.y,
                   absolute(to.x - from.x) + 1, absolute(to.y - from.y) + 1};
    if (rect_is_empty(rect_intersection(canvas->clip, bounds))) {
        return;
    }
    if (from.x == to.x || from.y == to.y) {
        canvas_fill_rect(canvas, bounds, ink.bright);
        return;
    }
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
    bool horizontal = absolute(to.x - from.x) >= absolute(to.y - from.y);
    for (int side = -1; side <= 1; side += 2) {
        int x = horizontal ? 0 : side;
        int y = horizontal ? side : 0;
        line(canvas, (Point){from.x + x, from.y + y}, (Point){to.x + x, to.y + y},
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

void neon_art_render(Rect area, Color background, FrameDrawer drawer, FrameSink sink)
{
    StripRenderer renderer = {.strip_pixels = playing_work_styles()->neon.pixels,
                              .area = area,
                              .strip_height = PLAYING_NEON_PIXELS / area.width,
                              .background = background};
    strip_renderer_render(&renderer, drawer, sink);
}
