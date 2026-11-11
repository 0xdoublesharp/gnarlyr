#include "gfx/strip_renderer.h"

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static void render_strip(const StripRenderer *renderer, Rect strip, FrameDrawer drawer,
                         FrameSink sink)
{
    Canvas canvas = canvas_make(renderer->strip_pixels, strip);
    if (!renderer->drawer_paints_everything) {
        canvas_clear(&canvas, renderer->background);
    }
    drawer.draw(drawer.context, &canvas);
    sink.write(sink.context, strip, renderer->strip_pixels);
}

void strip_renderer_render(const StripRenderer *renderer, FrameDrawer drawer, FrameSink sink)
{
    Rect area = renderer->area;
    int bottom = area.y + area.height;
    for (int top = area.y; top < bottom; top += renderer->strip_height) {
        Rect strip = {area.x, top, area.width, min_of(renderer->strip_height, bottom - top)};
        render_strip(renderer, strip, drawer, sink);
    }
}
