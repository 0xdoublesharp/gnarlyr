#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "gfx/font.h"

#define BRAND_MARK_SUN_TOP COLOR_RGB(255, 210, 143)
#define BRAND_MARK_SUN_MIDDLE COLOR_RGB(255, 142, 149)
#define BRAND_MARK_SUN_BOTTOM COLOR_RGB(239, 85, 189)
#define BRAND_MARK_TILE_TOP COLOR_RGB(47, 26, 73)
#define BRAND_MARK_TILE_BOTTOM COLOR_RGB(29, 21, 56)
#define BRAND_MARK_OUTLINE COLOR_RGB(121, 84, 163)

enum {
    BRAND_MARK_UNITS = 128,
    BRAND_MARK_TILE_UNITS = 124,
    BRAND_MARK_DISC_UNITS = 96,
    BRAND_MARK_SMOOTH_SIZE = 24,
    BRAND_MARK_MIDDLE_STOP = 120,
    BRAND_MARK_STOP_SPAN = 256,
    BRAND_MARK_LIGHT_GREEN = 40,
    BRAND_MARK_LEAD_SHARE = 4,
    BRAND_MARK_LEAD_PARTS = 5,
    BRAND_MARK_LEAD_GAP_PARTS = 3
};

typedef struct BrandMark {
    Rect area;
    Color behind;
    uint8_t alpha;
    bool tile;
} BrandMark;

typedef struct BrandMarkGap {
    int start;
    int end;
} BrandMarkGap;

BrandMarkGap brand_mark_gap(int size, int band);
Color brand_mark_sun_row(int row, int size);
int brand_mark_disc_size(int size, bool tile);
void brand_mark_draw(Canvas *canvas, BrandMark mark);

static inline bool brand_mark_needs_tile(Color behind)
{
    return (behind >> 5 & 63) >= BRAND_MARK_LIGHT_GREEN;
}

static inline Rect brand_mark_lead_area(Rect row, const Font *font)
{
    int size = font->ascent * BRAND_MARK_LEAD_SHARE / BRAND_MARK_LEAD_PARTS;
    return (Rect){row.x, row.y + (row.height - size) / 2, size, size};
}

static inline int brand_mark_lead_step(Rect mark)
{
    return mark.width + mark.width / BRAND_MARK_LEAD_GAP_PARTS;
}
