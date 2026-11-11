#include "gfx/image.h"

#include "image_columns.h"

enum {
    SMOOTH_SHIFT = 8,
    SMOOTH_ONE = 1 << SMOOTH_SHIFT,
    SMOOTH_MASK = SMOOTH_ONE - 1
};

static unsigned light_at(QuadLight light, Column column, int quad_width)
{
    return (unsigned)image_interpolate(light.left, light.right, column.offset, quad_width);
}

static void draw_lit_column(Canvas *canvas, Column column, const Image *image, int quad_width,
                            unsigned light)
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
        *target =
            color_scale(sampler.column[(int)(sampler.position >> IMAGE_STEP_SHIFT) * width], light);
        target += stride;
        sampler.position += sampler.step;
    }
}

static void draw_reflected_column(Canvas *canvas, Column column, const Image *image, int quad_width,
                                  unsigned light, int rows)
{
    Rect clip = canvas->clip;
    int top = column.top + column.height;
    int first = image_max(top, clip.y);
    int last = image_min(top + rows, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int skip = first - top;
    int stride = canvas->bounds.width;
    int width = image->width;
    int last_row = image->height - 1;
    Sampler sampler = image_sampler_at(&column, image, quad_width, skip);
    uint32_t fade_step = ((uint32_t)light << COLOR_LIGHT_SHIFT) / (uint32_t)rows;
    uint32_t faded = ((uint32_t)light << COLOR_LIGHT_SHIFT) - fade_step * (uint32_t)skip;
    Color *target = image_pixel_address(canvas, column.x, first);
    for (int count = last - first; count > 0; count--) {
        int row = image_max(last_row - (int)(sampler.position >> IMAGE_STEP_SHIFT), 0);
        *target = color_scale(sampler.column[row * width], faded >> COLOR_LIGHT_SHIFT);
        target += stride;
        sampler.position += sampler.step;
        faded -= fade_step;
    }
}

static void fill_column(Canvas *canvas, Column column, Color color)
{
    Rect clip = canvas->clip;
    int first = image_max(column.top, clip.y);
    int last = image_min(column.top + column.height, clip.y + clip.height);
    for (int y = first; y < last; y++) {
        *image_pixel_address(canvas, column.x, y) = color;
    }
}

void image_draw_quad_lit(Canvas *canvas, Image image, Quad quad, QuadLight light)
{
    if (light.left >= COLOR_LIGHT_FULL && light.right >= COLOR_LIGHT_FULL) {
        image_draw_quad(canvas, image, quad);
        return;
    }
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !image_quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = image_column_at(quad, x);
        if (column.height > 0) {
            draw_lit_column(canvas, column, &image, quad.width,
                            light_at(light, column, quad.width));
        }
    }
}

void image_draw_reflection(Canvas *canvas, Image image, Quad quad, QuadLight light, int rows)
{
    int first = 0;
    int last = 0;
    if (rows <= 0 || image.width <= 0 || image.height <= 0 ||
        !image_quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = image_column_at(quad, x);
        if (column.height > 0) {
            draw_reflected_column(canvas, column, &image, quad.width,
                                  light_at(light, column, quad.width), rows);
        }
    }
}

static int smooth_coordinate(int offset, int source_size, int target_size)
{
    int center = (2 * offset + 1) * source_size * (SMOOTH_ONE / 2) / target_size;
    return image_min(image_max(center - SMOOTH_ONE / 2, 0), (source_size - 1) * SMOOTH_ONE);
}

static Color smooth_mix(Color first, Color last, int fraction)
{
    uint32_t mask = 0x07E0F81Fu;
    uint32_t from = ((uint32_t)first | (uint32_t)first << 16) & mask;
    uint32_t to = ((uint32_t)last | (uint32_t)last << 16) & mask;
    unsigned weight = (unsigned)fraction >> 3;
    uint32_t mixed = ((from * (32u - weight) + to * weight) >> 5) & mask;
    return (Color)(mixed | mixed >> 16);
}

static Color smooth_sample(const Image *image, int x, int y)
{
    int left = x >> SMOOTH_SHIFT;
    int top = y >> SMOOTH_SHIFT;
    int right = image_min(left + 1, image->width - 1);
    int bottom = image_min(top + 1, image->height - 1);
    Color first = smooth_mix(image->pixels[top * image->width + left],
                             image->pixels[top * image->width + right], x & SMOOTH_MASK);
    Color last = smooth_mix(image->pixels[bottom * image->width + left],
                            image->pixels[bottom * image->width + right], x & SMOOTH_MASK);
    return smooth_mix(first, last, y & SMOOTH_MASK);
}

typedef struct SmoothColumn {
    int source_x;
    unsigned light;
} SmoothColumn;

static void draw_smooth_column(Canvas *canvas, Column column, const Image *image, SmoothColumn ink)
{
    int first = image_max(column.top, canvas->clip.y);
    int last = image_min(column.top + column.height, canvas->clip.y + canvas->clip.height);
    for (int y = first; y < last; y++) {
        int source_y = smooth_coordinate(y - column.top, image->height, column.height);
        Color color = smooth_sample(image, ink.source_x, source_y);
        *image_pixel_address(canvas, column.x, y) = color_scale(color, ink.light);
    }
}

void image_draw_quad_smooth(Canvas *canvas, Image image, Quad quad, QuadLight light)
{
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !image_quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = image_column_at(quad, x);
        if (column.height > 0) {
            SmoothColumn ink = {smooth_coordinate(column.offset, image.width, quad.width),
                                light_at(light, column, quad.width)};
            draw_smooth_column(canvas, column, &image, ink);
        }
    }
}

void image_fill_quad(Canvas *canvas, Quad quad, Color color)
{
    int first = 0;
    int last = 0;
    if (!image_quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = image_column_at(quad, x);
        if (column.height > 0) {
            fill_column(canvas, column, color);
        }
    }
}
