#include "ui/icons.h"

#include <stdbool.h>

enum {
    PAUSE_BAR_TENTHS = 3,
    TENTHS = 10,
    SKIP_BAR_PARTS = 6,
    BOLT_STROKE_TENTHS = 5,
    BOLT_UPPER_START_TENTHS = 5,
    BOLT_LOWER_END_TENTHS = 5
};

static void draw_triangle(Canvas *canvas, Rect area, Color color, bool pointing_right)
{
    for (int row = 0; row < area.height; row++) {
        int reach = row < area.height / 2 ? row : area.height - 1 - row;
        int width = 2 * reach + 1 < area.width ? 2 * reach + 1 : area.width;
        int left = pointing_right ? area.x : area.x + area.width - width;
        canvas_fill_rect(canvas, (Rect){left, area.y + row, width, 1}, color);
    }
}

void icon_draw_play(Canvas *canvas, Rect area, Color color)
{
    draw_triangle(canvas, area, color, true);
}

static int skip_bar_width(Rect area)
{
    int bar = area.width / SKIP_BAR_PARTS;
    return bar > 1 ? bar : 1;
}

void icon_draw_next(Canvas *canvas, Rect area, Color color)
{
    int bar = skip_bar_width(area);
    int half = (area.width - bar) / 2;
    draw_triangle(canvas, (Rect){area.x, area.y, half, area.height}, color, true);
    draw_triangle(canvas, (Rect){area.x + half, area.y, half, area.height}, color, true);
    canvas_fill_rect(canvas, (Rect){area.x + area.width - bar, area.y, bar, area.height}, color);
}

void icon_draw_previous(Canvas *canvas, Rect area, Color color)
{
    int bar = skip_bar_width(area);
    int half = (area.width - bar) / 2;
    canvas_fill_rect(canvas, (Rect){area.x, area.y, bar, area.height}, color);
    draw_triangle(canvas, (Rect){area.x + bar, area.y, half, area.height}, color, false);
    draw_triangle(canvas, (Rect){area.x + bar + half, area.y, half, area.height}, color, false);
}

void icon_draw_pause(Canvas *canvas, Rect area, Color color)
{
    int bar = area.width * PAUSE_BAR_TENTHS / TENTHS;
    bar = bar > 0 ? bar : 1;
    canvas_fill_rect(canvas, (Rect){area.x, area.y, bar, area.height}, color);
    canvas_fill_rect(canvas, (Rect){area.x + area.width - bar, area.y, bar, area.height}, color);
}

void icon_draw_stop(Canvas *canvas, Rect area, Color color)
{
    canvas_fill_rect(canvas, (Rect){area.x, area.y + 1, area.width - 2, area.height - 2}, color);
}

static void fill_row(Canvas *canvas, Rect area, int row, int left, int width, Color color)
{
    int clipped_left = left < 0 ? 0 : left;
    int right = left + width > area.width ? area.width : left + width;
    if (right > clipped_left) {
        canvas_fill_rect(
            canvas, (Rect){area.x + clipped_left, area.y + row, right - clipped_left, 1}, color);
    }
}

void icon_draw_bolt(Canvas *canvas, Rect area, Color color)
{
    int middle = area.height / 2;
    int stroke = area.width * BOLT_STROKE_TENTHS / TENTHS;
    int upper_start = area.width * BOLT_UPPER_START_TENTHS / TENTHS;
    int lower_end = area.width * BOLT_LOWER_END_TENTHS / TENTHS;
    int stroke_width = stroke > 0 ? stroke : 1;
    for (int row = 0; row < area.height; row++) {
        if (row < middle) {
            int left = upper_start - upper_start * row / (middle > 0 ? middle : 1);
            fill_row(canvas, area, row, left, stroke_width, color);
        } else if (row == middle) {
            fill_row(canvas, area, row, 0, area.width, color);
        } else {
            int span = area.height - 1 - middle;
            int right =
                area.width - (area.width - lower_end) * (row - middle) / (span > 0 ? span : 1);
            fill_row(canvas, area, row, right - stroke_width, stroke_width, color);
        }
    }
}

static int along(int row, int from_row, int from_x, int to_row, int to_x)
{
    int span = to_row - from_row;
    return span == 0 ? from_x : from_x + (to_x - from_x) * (row - from_row) / span;
}

static void dot(Canvas *canvas, Rect area, int x, int row, Color color)
{
    canvas_fill_rect(canvas, (Rect){area.x + x, area.y + row, 1, 1}, color);
}

void icon_draw_bluetooth(Canvas *canvas, Rect area, Color color)
{
    int last = area.height - 1;
    int quarter = last / 4;
    int lower = last - quarter;
    int right = area.width - 1;
    int middle = right / 2;
    canvas_fill_rect(canvas, (Rect){area.x + middle, area.y, 1, area.height}, color);
    for (int row = 0; row <= last; row++) {
        if (row <= quarter) {
            dot(canvas, area, along(row, 0, middle, quarter, right), row, color);
        } else if (row >= lower) {
            dot(canvas, area, along(row, lower, right, last, middle), row, color);
        }
        if (row >= quarter && row <= lower) {
            dot(canvas, area, along(row, quarter, right, lower, 0), row, color);
            dot(canvas, area, along(row, quarter, 0, lower, right), row, color);
        }
    }
}

void icon_draw_lock(Canvas *canvas, Rect area, Color color)
{
    int body_top = area.y + area.height * 2 / 5;
    int stroke = area.width / 5 > 0 ? area.width / 5 : 1;
    int inset = area.width / 5;
    canvas_fill_rect(canvas, (Rect){area.x, body_top, area.width, area.y + area.height - body_top},
                     color);
    canvas_fill_rect(canvas, (Rect){area.x + inset, area.y, area.width - 2 * inset, stroke}, color);
    canvas_fill_rect(canvas, (Rect){area.x + inset, area.y, stroke, body_top - area.y}, color);
    canvas_fill_rect(
        canvas, (Rect){area.x + area.width - inset - stroke, area.y, stroke, body_top - area.y},
        color);
}

enum {
    BITMAP_ROWS = 14,
    NOTE_WIDTH = 8,
    DISC_WIDTH = 14,
    SHUFFLE_WIDTH = 14,
    BADGE_GAP = 1
};

static const uint16_t NOTE_BITS[BITMAP_ROWS] = {48, 48, 112, 240, 176, 176, 176,
                                                48, 60, 62,  63,  63,  30,  12};
static const uint16_t DISC_BITS[BITMAP_ROWS] = {480,   2040,  4092, 8190, 8190, 16191, 15903,
                                                15903, 16191, 8190, 8190, 4092, 2040,  480};
static const uint16_t SHUFFLE_BITS[BITMAP_ROWS] = {0,  1024, 3072, 16143, 3480, 1264, 96,
                                                   96, 1264, 3480, 16143, 3072, 1024, 0};

static void draw_bits(Canvas *canvas, int x, int y, const uint16_t *rows, Color color)
{
    for (int row = 0; row < BITMAP_ROWS; row++) {
        for (int column = 0; rows[row] >> column != 0; column++) {
            if ((rows[row] >> column & 1u) != 0) {
                canvas_fill_rect(canvas, (Rect){x + column, y + row, 1, 1}, color);
            }
        }
    }
}

int icon_shuffle_width(bool albums)
{
    return (albums ? DISC_WIDTH : NOTE_WIDTH) + BADGE_GAP + SHUFFLE_WIDTH;
}

void icon_draw_shuffle(Canvas *canvas, Rect area, Color color, bool albums)
{
    int top = area.y + (area.height - BITMAP_ROWS) / 2;
    draw_bits(canvas, area.x, top, albums ? DISC_BITS : NOTE_BITS, color);
    int badge = albums ? DISC_WIDTH : NOTE_WIDTH;
    draw_bits(canvas, area.x + badge + BADGE_GAP, top, SHUFFLE_BITS, color);
}
