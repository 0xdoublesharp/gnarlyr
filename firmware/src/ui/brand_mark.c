#include "ui/brand_mark.h"

enum {
    SUB_SHIFT = 4,
    SUB = 1 << SUB_SHIFT,
    CORNER_UNITS = 28,
    STROKE_SPAN = 80,
    GAP_SPAN = 192,
    FULL_ALPHA = 255
};

static const uint8_t GAP_EDGES[] = {118, 129, 154, 167};
static const Color SUN[] = {BRAND_MARK_SUN_TOP, BRAND_MARK_SUN_MIDDLE, BRAND_MARK_SUN_BOTTOM};
static const Color FACE[] = {BRAND_MARK_TILE_TOP, BRAND_MARK_TILE_BOTTOM, BRAND_MARK_TILE_BOTTOM};
static const Color EDGE[] = {BRAND_MARK_OUTLINE, BRAND_MARK_OUTLINE, BRAND_MARK_OUTLINE};

typedef struct Round {
    Rect box;
    int radius;
    const Color *stops;
    int middle;
    int gaps[4];
} Round;

typedef struct Ink {
    Color behind;
    uint8_t alpha;
    bool smooth;
} Ink;

static int root_of(int value)
{
    int root = 0;
    for (int bit = 1 << 30; bit != 0; bit >>= 2) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

BrandMarkGap brand_mark_gap(int size, int band)
{
    int start = (GAP_EDGES[2 * band] * size + GAP_SPAN / 2) / GAP_SPAN;
    int end = (GAP_EDGES[2 * band + 1] * size + GAP_SPAN / 2) / GAP_SPAN;
    return (BrandMarkGap){start, end > start ? end : start + 1};
}

static Color shade(const Round *round, int row)
{
    int place = (2 * row + 1) * (BRAND_MARK_STOP_SPAN / 2) / round->box.height;
    int middle = round->middle;
    bool upper = place < middle;
    return color_between(round->stops[upper ? 0 : 1], round->stops[upper ? 1 : 2],
                         upper ? place : place - middle,
                         upper ? middle : BRAND_MARK_STOP_SPAN - middle);
}

Color brand_mark_sun_row(int row, int size)
{
    Round sun = {.box = {0, 0, size, size}, .stops = SUN, .middle = BRAND_MARK_MIDDLE_STOP};
    return shade(&sun, row);
}

static int row_inset(const Round *round, int row)
{
    int centre = (2 * row + 1) * SUB / 2;
    int reach = round->radius - centre;
    int lower = centre - (round->box.height * SUB - round->radius);
    reach = lower > reach ? lower : reach;
    if (reach <= 0) {
        return 0;
    }
    return reach >= round->radius
               ? -1
               : round->radius - root_of(round->radius * round->radius - reach * reach);
}

static void blend_at(Canvas *canvas, Point point, Color color, int cover)
{
    canvas_blend_pixel(canvas, point, color, (uint8_t)(cover * COLOR_ALPHA_OPAQUE / SUB));
}

static void fill_span(Canvas *canvas, Point from, int right, Color color)
{
    int left = from.x;
    int first = (left + SUB - 1) >> SUB_SHIFT;
    int last = right >> SUB_SHIFT;
    if (first >= last) {
        return;
    }
    canvas_fill_rect(canvas, (Rect){first, from.y, last - first, 1}, color);
    blend_at(canvas, (Point){first - 1, from.y}, color, first * SUB - left);
    blend_at(canvas, (Point){last, from.y}, color, right - last * SUB);
}

static void fill_row(Canvas *canvas, const Round *round, int row, const Ink *ink)
{
    int inset = row_inset(round, row);
    const int *gaps = round->gaps;
    if (inset < 0 || (row >= gaps[0] && row < gaps[1]) || (row >= gaps[2] && row < gaps[3])) {
        return;
    }
    if (!ink->smooth) {
        inset = (inset + SUB / 2) & ~(SUB - 1);
    }
    int left = round->box.x * SUB + inset;
    int right = (round->box.x + round->box.width) * SUB - inset;
    Color color = color_between(ink->behind, shade(round, row), ink->alpha, FULL_ALPHA);
    fill_span(canvas, (Point){left, round->box.y + row}, right, color);
}

static void fill_round(Canvas *canvas, const Round *round, const Ink *ink)
{
    int first = canvas->clip.y - round->box.y;
    int last = canvas->clip.y + canvas->clip.height - round->box.y;
    first = first > 0 ? first : 0;
    last = last < round->box.height ? last : round->box.height;
    for (int row = first; row < last; row++) {
        fill_row(canvas, round, row, ink);
    }
}

static Rect square_in(Rect area, int size)
{
    return (Rect){area.x + (area.width - size) / 2, area.y + (area.height - size) / 2, size, size};
}

int brand_mark_disc_size(int size, bool tile)
{
    return tile ? (size * BRAND_MARK_DISC_UNITS + BRAND_MARK_UNITS / 2) / BRAND_MARK_UNITS : size;
}

static void draw_tile(Canvas *canvas, Rect mark, const Ink *ink)
{
    int size = (mark.width * BRAND_MARK_TILE_UNITS + BRAND_MARK_UNITS / 2) / BRAND_MARK_UNITS;
    int stroke = (mark.width + STROKE_SPAN / 2) / STROKE_SPAN;
    stroke = stroke > 0 ? stroke : 1;
    Round round = {.box = square_in(mark, size),
                   .radius = size * SUB * CORNER_UNITS / BRAND_MARK_TILE_UNITS,
                   .stops = EDGE,
                   .middle = BRAND_MARK_STOP_SPAN};
    fill_round(canvas, &round, ink);
    round.box = square_in(mark, size - 2 * stroke);
    round.radius -= stroke * SUB;
    round.stops = FACE;
    fill_round(canvas, &round, ink);
}

void brand_mark_draw(Canvas *canvas, BrandMark mark)
{
    int size = mark.area.width < mark.area.height ? mark.area.width : mark.area.height;
    Rect box = square_in(mark.area, size);
    Ink ink = {mark.behind, mark.alpha, size >= BRAND_MARK_SMOOTH_SIZE};
    if (mark.tile) {
        draw_tile(canvas, box, &ink);
    }
    int disc = brand_mark_disc_size(size, mark.tile);
    BrandMarkGap upper = brand_mark_gap(disc, 0);
    BrandMarkGap lower = brand_mark_gap(disc, 1);
    Round sun = {.box = square_in(box, disc),
                 .radius = disc * SUB / 2,
                 .stops = SUN,
                 .middle = BRAND_MARK_MIDDLE_STOP,
                 .gaps = {upper.start, upper.end, lower.start, lower.end}};
    fill_round(canvas, &sun, &ink);
}
