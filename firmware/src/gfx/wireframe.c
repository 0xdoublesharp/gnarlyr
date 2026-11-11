#include "gfx/wireframe.h"

#include <stdbool.h>

enum {
    FIXED_SHIFT = 6,
    FIXED_ONE = 1 << FIXED_SHIFT,
    TURN_SHIFT = 14,
    PERSPECTIVE_SHIFT = 12,
    SUBPIXEL_SHIFT = 4,
    MINOR_SHIFT = 12,
    FRACTION_SHIFT = MINOR_SHIFT - SUBPIXEL_SHIFT,
    MINOR_ONE = 1 << MINOR_SHIFT,
    FRACTION_ONE = 1 << FRACTION_SHIFT,
    DEPTH_MIDDLE = 128,
    SCREEN_SHIFT = FIXED_SHIFT + 7 - SUBPIXEL_SHIFT,
    NEAREST_DISTANCE = (WIREFRAME_DISTANCE << FIXED_SHIFT) / 2
};

static const int16_t QUARTER_SINE[WIREFRAME_QUARTER + 1] = {
    0,     402,   804,   1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,  4370,  4756,
    5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,  9102,  9434,
    9760,  10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
    13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384,
};

typedef struct Turned {
    int first;
    int second;
} Turned;

typedef struct LineAxes {
    int u0;
    int v0;
    int u1;
    int v1;
    bool steep;
} LineAxes;

int wireframe_sin(uint8_t angle)
{
    int index = angle % WIREFRAME_QUARTER;
    int quadrant = angle / WIREFRAME_QUARTER;
    int value = QUARTER_SINE[(quadrant & 1) != 0 ? WIREFRAME_QUARTER - index : index];
    return quadrant >= 2 ? -value : value;
}

int wireframe_cos(uint8_t angle)
{
    return wireframe_sin((uint8_t)(angle + WIREFRAME_QUARTER));
}

static Turned turn(int first, int second, uint8_t angle)
{
    int sine = wireframe_sin(angle);
    int cosine = wireframe_cos(angle);
    return (Turned){(first * cosine - second * sine) >> TURN_SHIFT,
                    (first * sine + second * cosine) >> TURN_SHIFT};
}

static int screen_axis(int center, int value, int perspective, int radius)
{
    int scaled = (value * perspective) >> PERSPECTIVE_SHIFT;
    return center * WIREFRAME_SUBPIXEL + ((scaled * radius) >> SCREEN_SHIFT);
}

WirePoint wireframe_project(WireVertex vertex, WirePlace place, const WireView *view)
{
    Turned spun = turn(vertex.x * FIXED_ONE, vertex.y * FIXED_ONE, place.spin);
    int x = spun.first * place.size / WIREFRAME_FULL_SIZE + place.x * FIXED_ONE;
    int y = spun.second * place.size / WIREFRAME_FULL_SIZE + place.y * FIXED_ONE;
    int z = vertex.z * FIXED_ONE * place.size / WIREFRAME_FULL_SIZE + place.z * FIXED_ONE;
    Turned yawed = turn(x, z, view->yaw);
    Turned pitched = turn(y, yawed.second, view->pitch);
    int distance = (WIREFRAME_DISTANCE << FIXED_SHIFT) + pitched.second;
    distance = distance < NEAREST_DISTANCE ? NEAREST_DISTANCE : distance;
    int perspective = (int)(((uint32_t)WIREFRAME_DISTANCE << (FIXED_SHIFT + PERSPECTIVE_SHIFT)) /
                            (uint32_t)distance);
    return (WirePoint){
        .x = screen_axis(view->center.x, yawed.first, perspective, view->radius),
        .y = screen_axis(view->center.y, pitched.first, perspective, view->radius),
        .depth = DEPTH_MIDDLE - (pitched.second >> FIXED_SHIFT),
    };
}

static int absolute(int value)
{
    return value < 0 ? -value : value;
}

static LineAxes axes_of(WirePoint from, WirePoint to)
{
    bool steep = absolute(to.y - from.y) > absolute(to.x - from.x);
    LineAxes axes = steep ? (LineAxes){from.y, from.x, to.y, to.x, true}
                          : (LineAxes){from.x, from.y, to.x, to.y, false};
    if (axes.u0 > axes.u1) {
        axes = (LineAxes){axes.u1, axes.v1, axes.u0, axes.v0, steep};
    }
    return axes;
}

static Point clip_span(const Canvas *canvas, bool along_y)
{
    int low = along_y ? canvas->clip.y : canvas->clip.x;
    return (Point){low, low + (along_y ? canvas->clip.height : canvas->clip.width) - 1};
}

static bool misses_minor(const Canvas *canvas, const LineAxes *axes)
{
    Point span = clip_span(canvas, !axes->steep);
    int lowest = axes->v0 < axes->v1 ? axes->v0 : axes->v1;
    int highest = axes->v0 > axes->v1 ? axes->v0 : axes->v1;
    return (highest >> SUBPIXEL_SHIFT) + 1 < span.x || (lowest >> SUBPIXEL_SHIFT) > span.y;
}

static void blend(Canvas *canvas, const LineAxes *axes, Point at, WireStroke stroke, int weight)
{
    Point pixel = axes->steep ? (Point){at.y, at.x} : at;
    int alpha = stroke.alpha * weight >> SUBPIXEL_SHIFT;
    if (alpha > 0) {
        canvas_blend_pixel(canvas, pixel, stroke.color, (uint8_t)alpha);
    }
}

void wireframe_line(Canvas *canvas, WirePoint from, WirePoint to, WireStroke stroke)
{
    LineAxes axes = axes_of(from, to);
    Point major_span = clip_span(canvas, axes.steep);
    int first = (axes.u0 + WIREFRAME_SUBPIXEL - 1) >> SUBPIXEL_SHIFT;
    int last = axes.u1 >> SUBPIXEL_SHIFT;
    first = first < major_span.x ? major_span.x : first;
    last = last > major_span.y ? major_span.y : last;
    if (first > last || misses_minor(canvas, &axes)) {
        return;
    }
    int span = axes.u1 - axes.u0;
    int gradient = span == 0 ? 0 : (axes.v1 - axes.v0) * MINOR_ONE / span;
    int minor = axes.v0 * FRACTION_ONE +
                ((first * WIREFRAME_SUBPIXEL - axes.u0) * gradient >> SUBPIXEL_SHIFT);
    for (int major = first; major <= last; major++, minor += gradient) {
        int base = minor >> MINOR_SHIFT;
        int fraction = (minor >> FRACTION_SHIFT) & (WIREFRAME_SUBPIXEL - 1);
        blend(canvas, &axes, (Point){major, base}, stroke, WIREFRAME_SUBPIXEL - fraction);
        blend(canvas, &axes, (Point){major, base + 1}, stroke, fraction);
    }
}
