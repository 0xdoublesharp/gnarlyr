#pragma once

#include <stdbool.h>

#include "gfx/image.h"

typedef struct Column {
    int x;
    int offset;
    int top;
    int height;
} Column;

enum {
    IMAGE_STEP_SHIFT = 16
};

typedef struct Sampler {
    const Color *column;
    uint32_t position;
    uint32_t step;
} Sampler;

static inline int image_max(int first, int second)
{
    return first > second ? first : second;
}

static inline int image_min(int first, int second)
{
    return first < second ? first : second;
}

static inline int image_interpolate(int from, int to, int offset, int span)
{
    return from + (to - from) * offset / span;
}

static inline Column image_column_at(Quad quad, int x)
{
    int offset = x - quad.left;
    return (Column){
        .x = x,
        .offset = offset,
        .top = image_interpolate(quad.left_top, quad.right_top, offset, quad.width),
        .height = image_interpolate(quad.left_height, quad.right_height, offset, quad.width),
    };
}

static inline Color *image_pixel_address(Canvas *canvas, int x, int y)
{
    Rect bounds = canvas->bounds;
    return &canvas->pixels[(y - bounds.y) * bounds.width + (x - bounds.x)];
}

static inline Sampler image_sampler_at(const Column *column, const Image *image, int quad_width,
                                       int skip)
{
    uint32_t height = (uint32_t)column->height;
    uint32_t step = (((uint32_t)image->height << IMAGE_STEP_SHIFT) + height - 1) / height;
    return (Sampler){
        .column = &image->pixels[column->offset * image->width / quad_width],
        .position = step * (uint32_t)skip,
        .step = step,
    };
}

static inline bool image_quad_visible(const Canvas *canvas, Quad quad, int *first, int *last)
{
    Rect clip = canvas->clip;
    *first = image_max(quad.left, clip.x);
    *last = image_min(quad.left + quad.width, clip.x + clip.width);
    return quad.width > 0 && *first < *last;
}
