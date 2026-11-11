#include "gfx/image.h"

#include "image_columns.h"

static void draw_column(Canvas *canvas, Column column, const Image *image, int quad_width)
{
    Rect clip = canvas->clip;
    int first = image_max(column.top, clip.y);
    int last = image_min(column.top + column.height, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int stride = canvas->bounds.width;
    int width = image->width;
    Sampler sampler = image_sampler_at(&column, image, quad_width, first - column.top);
    Color *target = image_pixel_address(canvas, column.x, first);
    for (int count = last - first; count > 0; count--) {
        *target = sampler.column[(int)(sampler.position >> IMAGE_STEP_SHIFT) * width];
        target += stride;
        sampler.position += sampler.step;
    }
}

void image_draw_quad(Canvas *canvas, Image image, Quad quad)
{
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !image_quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = image_column_at(quad, x);
        if (column.height > 0) {
            draw_column(canvas, column, &image, quad.width);
        }
    }
}
