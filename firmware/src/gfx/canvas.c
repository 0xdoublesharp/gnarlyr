#include "gfx/canvas.h"

enum {
    PIXELS_PER_ROUND = 8
};

static Color *pixel_address(const Canvas *canvas, int x, int y)
{
    int column = x - canvas->bounds.x;
    int row = y - canvas->bounds.y;
    return &canvas->pixels[row * canvas->bounds.width + column];
}

static bool contains(Rect area, Point point)
{
    Rect single = {point.x, point.y, 1, 1};
    return !rect_is_empty(rect_intersection(area, single));
}

Canvas canvas_make(Color *pixels, Rect bounds)
{
    return (Canvas){.pixels = pixels, .bounds = bounds, .clip = bounds};
}

Canvas canvas_clipped(const Canvas *canvas, Rect area)
{
    Canvas clipped = *canvas;
    clipped.clip = rect_intersection(canvas->clip, area);
    return clipped;
}

Canvas canvas_shifted(const Canvas *canvas, int dx, Rect area)
{
    Canvas shifted = *canvas;
    shifted.bounds.x -= dx;
    shifted.clip.x -= dx;
    shifted.clip = rect_intersection(shifted.clip, area);
    return shifted;
}

static void fill_span(Color *pixels, int count, Color color)
{
    uint32_t pair = (uint32_t)color | (uint32_t)color << 16;
    if (((uintptr_t)pixels & 2u) != 0 && count > 0) {
        *pixels++ = color;
        count--;
    }
    for (; count >= PIXELS_PER_ROUND; count -= PIXELS_PER_ROUND) {
        __builtin_memcpy(pixels, &pair, sizeof pair);
        __builtin_memcpy(pixels + 2, &pair, sizeof pair);
        __builtin_memcpy(pixels + 4, &pair, sizeof pair);
        __builtin_memcpy(pixels + 6, &pair, sizeof pair);
        pixels += PIXELS_PER_ROUND;
    }
    for (; count > 0; count--) {
        *pixels++ = color;
    }
}

void canvas_clear(Canvas *canvas, Color color)
{
    canvas_fill_rect(canvas, canvas->clip, color);
}

void canvas_fill_rect(Canvas *canvas, Rect area, Color color)
{
    Rect visible = rect_intersection(canvas->clip, area);
    if (rect_is_empty(visible)) {
        return;
    }
    if (visible.x == canvas->bounds.x && visible.width == canvas->bounds.width) {
        fill_span(pixel_address(canvas, visible.x, visible.y), visible.width * visible.height,
                  color);
        return;
    }
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        fill_span(pixel_address(canvas, visible.x, y), visible.width, color);
    }
}

void canvas_fill_gradient(Canvas *canvas, Rect area, Color top, Color bottom)
{
    Rect visible = rect_intersection(canvas->clip, area);
    if (rect_is_empty(visible)) {
        return;
    }
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        Color color = color_between(top, bottom, y - area.y, area.height - 1);
        fill_span(pixel_address(canvas, visible.x, y), visible.width, color);
    }
}

void canvas_blend_pixel(Canvas *canvas, Point point, Color color, uint8_t alpha)
{
    if (!contains(canvas->clip, point)) {
        return;
    }
    Color *pixel = pixel_address(canvas, point.x, point.y);
    *pixel = color_blend(*pixel, color, alpha);
}

Color canvas_pixel_at(const Canvas *canvas, int x, int y)
{
    return contains(canvas->bounds, (Point){x, y}) ? *pixel_address(canvas, x, y) : 0;
}
