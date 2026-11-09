#include "ui/icons.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    OP_FILL,
    OP_LINE,
    OP_RECT,
    OP_RING,
    OP_TRI
};

enum {
    TOP_LEFT = 1,
    TOP_RIGHT = 2,
    BOTTOM_LEFT = 4,
    BOTTOM_RIGHT = 8,
    TOP = TOP_LEFT | TOP_RIGHT,
    BOTTOM = BOTTOM_LEFT | BOTTOM_RIGHT,
    LEFT = TOP_LEFT | BOTTOM_LEFT,
    RIGHT = TOP_RIGHT | BOTTOM_RIGHT,
    ROUND = 4,
    GAP_SHIFT = 4,
    POINT_RIGHT = 0,
    POINT_LEFT = 1,
    POINT_UP = 2,
    POINT_DOWN = 3,
    FIELD_MASK = 31,
    EXTRA_MASK = 127,
    PEN_MASK = 3
};

#define ICON_ACCENT_BIT (1u << 28)
#define ICON_LAST_BIT (1u << 27)
#define ICON_OP(kind, a, b, c, d, e)                                                               \
    (((uint32_t)(kind) << 29) | ((uint32_t)(a) << 22) | ((uint32_t)(b) << 17) |                    \
     ((uint32_t)(c) << 12) | ((uint32_t)(d) << 7) | (uint32_t)(e))
#define FILL(x0, y0, x1, y1) ICON_OP(OP_FILL, x0, y0, x1, y1, 0)
#define BLOCK(x0, y0, x1, y1) ICON_OP(OP_FILL, x0, y0, x1, y1, ROUND)
#define LINE(x0, y0, x1, y1) ICON_OP(OP_LINE, x0, y0, x1, y1, 2)
#define THIN(x0, y0, x1, y1) ICON_OP(OP_LINE, x0, y0, x1, y1, 1)
#define THICK(x0, y0, x1, y1) ICON_OP(OP_LINE, x0, y0, x1, y1, 3)
#define BOX(x0, y0, x1, y1) ICON_OP(OP_RECT, x0, y0, x1, y1, 2 | ROUND)
#define RING(x0, y0, size) ICON_OP(OP_RING, x0, y0, size, 2, TOP | BOTTOM)
#define ARC(x0, y0, size, quadrants) ICON_OP(OP_RING, x0, y0, size, 2, quadrants)
#define OPEN_RING(x0, y0, size, gap) ICON_OP(OP_RING, x0, y0, size, 2, TOP | BOTTOM | (gap) << 4)
#define DISC(x0, y0, size) ICON_OP(OP_RING, x0, y0, size, 0, TOP | BOTTOM)
#define HALF_DISC(x0, y0, size, quadrants) ICON_OP(OP_RING, x0, y0, size, 0, quadrants)
#define TRIANGLE(x0, y0, x1, y1, point) ICON_OP(OP_TRI, x0, y0, x1, y1, point)
#define ACCENT(op) ((op) | ICON_ACCENT_BIT)
#define LAST(op) ((op) | ICON_LAST_BIT)

#define MUSIC_GLYPH                                                                                \
    ACCENT(DISC(1, 13, 6)), ACCENT(DISC(11, 11, 6)), LINE(5, 3, 5, 15), LINE(15, 1, 15, 13),       \
        LAST(THICK(5, 3, 15, 1))
#define AUDIOBOOK_GLYPH                                                                            \
    BOX(3, 1, 16, 18), ACCENT(FILL(3, 1, 5, 18)), LAST(ACCENT(FILL(10, 1, 12, 8)))
#define PODCAST_GLYPH                                                                              \
    ACCENT(BLOCK(7, 1, 12, 11)), ARC(3, 2, 14, BOTTOM), LINE(9, 15, 9, 17),                        \
        LAST(LINE(5, 18, 13, 18))
#define NOW_PLAYING_GLYPH RING(1, 1, 18), LAST(ACCENT(TRIANGLE(8, 5, 14, 13, POINT_RIGHT)))
#define MIXTAPE_GLYPH                                                                              \
    BOX(0, 2, 19, 16), ACCENT(DISC(3, 5, 5)), ACCENT(DISC(12, 5, 5)), LINE(8, 6, 11, 6),           \
        LAST(BOX(5, 12, 14, 16))
#define AUDIO_GLYPH                                                                                \
    ARC(2, 2, 16, TOP), ACCENT(BLOCK(1, 10, 5, 17)), LAST(ACCENT(BLOCK(14, 10, 18, 17)))
#define SETTINGS_GLYPH                                                                             \
    LINE(1, 3, 17, 3), LINE(1, 9, 17, 9), LINE(1, 15, 17, 15), ACCENT(BLOCK(6, 1, 9, 6)),          \
        ACCENT(BLOCK(12, 7, 15, 12)), LAST(ACCENT(BLOCK(3, 13, 6, 18)))
#define COVER_FLOW_GLYPH                                                                           \
    ACCENT(BLOCK(5, 3, 14, 12)), TRIANGLE(0, 4, 3, 11, POINT_LEFT),                                \
        TRIANGLE(16, 4, 19, 11, POINT_RIGHT), LAST(LINE(5, 16, 13, 16))
#define ARTIST_GLYPH ACCENT(DISC(6, 1, 8)), LAST(ARC(2, 11, 16, TOP))
#define ALBUM_ARTIST_GLYPH                                                                         \
    RING(10, 0, 10), DISC(14, 4, 2), ACCENT(DISC(2, 4, 6)), LAST(ARC(0, 12, 12, TOP))
#define ALBUM_GLYPH ARC(6, 2, 14, RIGHT), BOX(1, 3, 13, 16), LAST(ACCENT(DISC(5, 8, 5)))
#define GENRE_GLYPH                                                                                \
    LINE(1, 9, 6, 4), LINE(1, 9, 6, 14), LINE(6, 4, 17, 4), LINE(17, 4, 17, 14),                   \
        LINE(6, 14, 17, 14), LAST(ACCENT(DISC(6, 8, 4)))
#define FOLDER_GLYPH ACCENT(BLOCK(1, 3, 8, 6)), LAST(BOX(1, 6, 18, 17))
#define SONG_GLYPH                                                                                 \
    ACCENT(DISC(3, 12, 7)), LINE(8, 2, 8, 14), LINE(9, 2, 14, 6), LAST(LINE(14, 6, 14, 9))
#define PLAYLIST_GLYPH                                                                             \
    LINE(1, 3, 11, 3), LINE(1, 8, 11, 8), LINE(1, 13, 7, 13), LINE(16, 4, 16, 15),                 \
        LAST(ACCENT(DISC(11, 12, 6)))
#define FAVORITE_GLYPH                                                                             \
    ACCENT(TRIANGLE(7, 0, 12, 9, POINT_UP)), ACCENT(TRIANGLE(0, 6, 19, 13, POINT_DOWN)),           \
        ACCENT(THICK(6, 11, 3, 16)), LAST(ACCENT(THICK(11, 11, 14, 16)))
#define SHUFFLE_GLYPH                                                                              \
    LINE(1, 4, 4, 4), LINE(4, 4, 13, 13), ACCENT(TRIANGLE(14, 10, 18, 17, POINT_RIGHT)),           \
        LINE(1, 13, 4, 13), LINE(4, 13, 13, 4), LAST(ACCENT(TRIANGLE(14, 1, 18, 8, POINT_RIGHT)))
#define REPEAT_GLYPH                                                                               \
    LINE(2, 4, 13, 4), LINE(1, 4, 1, 10), ACCENT(TRIANGLE(14, 1, 18, 8, POINT_RIGHT)),             \
        LINE(6, 14, 17, 14), LINE(17, 8, 17, 14), LAST(ACCENT(TRIANGLE(1, 11, 5, 18, POINT_LEFT)))
#define DISPLAY_GLYPH                                                                              \
    ACCENT(HALF_DISC(2, 2, 16, TOP)), LINE(0, 10, 18, 10), LINE(3, 14, 15, 14),                    \
        LAST(LINE(6, 18, 12, 18))
#define THEME_GLYPH RING(1, 1, 18), LAST(ACCENT(HALF_DISC(1, 1, 18, LEFT)))
#define POWER_GLYPH OPEN_RING(2, 3, 16, 3), LAST(ACCENT(LINE(9, 0, 9, 9)))
#define SLEEP_GLYPH ARC(1, 1, 18, LEFT), LAST(ICON_OP(OP_RING, 3, 1, 18, 3, LEFT))
#define TIMEOUT_GLYPH RING(1, 1, 18), ACCENT(LINE(9, 4, 9, 9)), LAST(ACCENT(LINE(10, 10, 12, 12)))
#define CONTROLS_GLYPH                                                                             \
    BOX(4, 0, 15, 19), ACCENT(FILL(7, 4, 12, 5)), ACCENT(FILL(9, 2, 10, 7)),                       \
        LAST(ACCENT(FILL(7, 14, 12, 15)))
#define START_GLYPH                                                                                \
    ACCENT(TRIANGLE(1, 1, 18, 8, POINT_UP)), BOX(4, 8, 15, 18), LAST(ACCENT(FILL(8, 12, 11, 18)))
#define RESCAN_GLYPH                                                                               \
    ARC(2, 2, 16, TOP | BOTTOM_RIGHT), LAST(ACCENT(TRIANGLE(3, 13, 8, 19, POINT_LEFT)))
#define DIAGNOSTICS_GLYPH                                                                          \
    BOX(1, 1, 18, 14), ACCENT(LINE(4, 10, 7, 5)), ACCENT(LINE(7, 5, 11, 10)),                      \
        ACCENT(LINE(11, 10, 14, 5)), LINE(9, 15, 9, 16), LAST(LINE(6, 17, 12, 17))
#define GAIN_GLYPH RING(1, 1, 18), LAST(ACCENT(THICK(8, 10, 12, 6)))
#define JACK_GLYPH                                                                                 \
    BLOCK(8, 0, 11, 2), FILL(8, 4, 11, 6), FILL(8, 8, 11, 11), ACCENT(BLOCK(6, 12, 13, 17)),       \
        LAST(FILL(9, 18, 10, 19))
#define USB_GLYPH                                                                                  \
    LINE(9, 5, 9, 15), ACCENT(TRIANGLE(7, 0, 12, 5, POINT_UP)), LINE(2, 6, 2, 7),                  \
        LINE(2, 8, 8, 11), ACCENT(DISC(1, 2, 4)), LINE(16, 8, 16, 10), LINE(11, 14, 16, 11),       \
        ACCENT(FILL(15, 4, 18, 7)), LAST(DISC(8, 16, 4))
#define BLUETOOTH_GLYPH                                                                            \
    LINE(9, 1, 9, 17), LINE(9, 1, 13, 5), LINE(13, 5, 5, 13), LINE(9, 17, 13, 13),                 \
        LAST(LINE(13, 13, 5, 5))
#define KEY_PLUS_GLYPH BOX(1, 1, 18, 18), ACCENT(LINE(9, 5, 9, 13)), LAST(ACCENT(LINE(5, 9, 13, 9)))
#define KEY_MINUS_GLYPH BOX(1, 1, 18, 18), LAST(ACCENT(LINE(5, 9, 13, 9)))
#define KEY_PLAY_GLYPH BOX(1, 1, 18, 18), LAST(ACCENT(TRIANGLE(7, 5, 13, 13, POINT_RIGHT)))
#define KEY_MENU_GLYPH                                                                             \
    BOX(1, 1, 18, 18), ACCENT(LINE(5, 5, 13, 5)), ACCENT(LINE(5, 9, 13, 9)),                       \
        LAST(ACCENT(LINE(5, 13, 13, 13)))
#define DISC_GLYPH LAST(DISC(0, 0, 20))
#define HUB_GLYPH LAST(ICON_OP(OP_RING, 0, 0, 20, 5, TOP | BOTTOM))
#define PLAY_GLYPH LAST(TRIANGLE(1, 0, 12, 12, POINT_RIGHT))
#define PAUSE_GLYPH FILL(1, 0, 4, 13), LAST(FILL(9, 0, 12, 13))
#define NEXT_GLYPH                                                                                 \
    TRIANGLE(1, 2, 6, 11, POINT_RIGHT), TRIANGLE(6, 2, 11, 11, POINT_RIGHT),                       \
        LAST(FILL(11, 2, 12, 11))
#define PREVIOUS_GLYPH                                                                             \
    FILL(1, 2, 2, 11), TRIANGLE(2, 2, 7, 11, POINT_LEFT), LAST(TRIANGLE(7, 2, 12, 11, POINT_LEFT))
#define SEEK_GLYPH                                                                                 \
    LINE(3, 6, 9, 6), TRIANGLE(0, 3, 3, 10, POINT_LEFT), LAST(TRIANGLE(10, 3, 13, 10, POINT_RIGHT))
#define QUEUE_GLYPH                                                                                \
    LINE(1, 2, 11, 2), LINE(1, 6, 5, 6), LINE(1, 10, 5, 10),                                       \
        LAST(ACCENT(TRIANGLE(9, 7, 13, 13, POINT_RIGHT)))
#define SPEAKER_GLYPH                                                                              \
    FILL(0, 5, 2, 8), TRIANGLE(2, 3, 6, 10, POINT_LEFT), LAST(ACCENT(ARC(3, 2, 10, RIGHT)))
#define BOLT_GLYPH THICK(8, 0, 3, 7), FILL(3, 6, 10, 7), LAST(THICK(9, 6, 4, 12))
#define BLUETOOTH_SMALL_GLYPH                                                                      \
    THIN(6, 0, 6, 13), THIN(6, 0, 10, 4), THIN(10, 4, 3, 11), THIN(6, 13, 10, 9),                  \
        LAST(THIN(10, 9, 3, 2))

static const uint32_t GLYPHS[] = {
    MUSIC_GLYPH,       AUDIOBOOK_GLYPH,
    PODCAST_GLYPH,     NOW_PLAYING_GLYPH,
    MIXTAPE_GLYPH,     AUDIO_GLYPH,
    SETTINGS_GLYPH,    COVER_FLOW_GLYPH,
    ARTIST_GLYPH,      ALBUM_ARTIST_GLYPH,
    ALBUM_GLYPH,       GENRE_GLYPH,
    FOLDER_GLYPH,      SONG_GLYPH,
    PLAYLIST_GLYPH,    FAVORITE_GLYPH,
    SHUFFLE_GLYPH,     REPEAT_GLYPH,
    DISPLAY_GLYPH,     THEME_GLYPH,
    POWER_GLYPH,       SLEEP_GLYPH,
    TIMEOUT_GLYPH,     CONTROLS_GLYPH,
    START_GLYPH,       RESCAN_GLYPH,
    DIAGNOSTICS_GLYPH, GAIN_GLYPH,
    JACK_GLYPH,        USB_GLYPH,
    BLUETOOTH_GLYPH,   KEY_PLUS_GLYPH,
    KEY_MINUS_GLYPH,   KEY_PLAY_GLYPH,
    KEY_MENU_GLYPH,    DISC_GLYPH,
    HUB_GLYPH,         PLAY_GLYPH,
    PAUSE_GLYPH,       NEXT_GLYPH,
    PREVIOUS_GLYPH,    SEEK_GLYPH,
    QUEUE_GLYPH,       SPEAKER_GLYPH,
    BOLT_GLYPH,        BLUETOOTH_SMALL_GLYPH,
};

typedef struct IconOp {
    int kind;
    int a;
    int b;
    int c;
    int d;
    int e;
} IconOp;

typedef struct Pen {
    Canvas *canvas;
    Rect area;
    int grid;
    Color color;
} Pen;

typedef struct Box {
    int left;
    int top;
    int right;
    int bottom;
} Box;

typedef struct RowSpan {
    int start;
    int width;
    int hole;
} RowSpan;

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int magnitude(int value)
{
    return value < 0 ? -value : value;
}

static IconOp decode(uint32_t word)
{
    return (IconOp){(int)(word >> 29),
                    (int)(word >> 22) & FIELD_MASK,
                    (int)(word >> 17) & FIELD_MASK,
                    (int)(word >> 12) & FIELD_MASK,
                    (int)(word >> 7) & FIELD_MASK,
                    (int)word & EXTRA_MASK};
}

static void emit(const Pen *pen, Box box)
{
    if (box.right < box.left || box.bottom < box.top) {
        return;
    }
    Rect area = pen->area;
    int left = area.x + box.left * area.width / pen->grid;
    int top = area.y + box.top * area.height / pen->grid;
    int right = area.x + (box.right + 1) * area.width / pen->grid;
    int bottom = area.y + (box.bottom + 1) * area.height / pen->grid;
    canvas_fill_rect(pen->canvas,
                     (Rect){left, top, max_of(right - left, 1), max_of(bottom - top, 1)},
                     pen->color);
}

static void draw_line(const Pen *pen, const IconOp *op)
{
    int x = op->a;
    int y = op->b;
    int dx = magnitude(op->c - x);
    int dy = -magnitude(op->d - y);
    int error = dx + dy;
    for (;;) {
        emit(pen, (Box){x, y, x + op->e - 1, y + op->e - 1});
        if (x == op->c && y == op->d) {
            return;
        }
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += x < op->c ? 1 : -1;
        }
        if (twice <= dx) {
            error += dx;
            y += y < op->d ? 1 : -1;
        }
    }
}

static int span_width(int size, int row)
{
    int offset = 2 * row + 1 - size;
    int width = size;
    while (width > 0 && width * width + offset * offset > size * size + size) {
        width -= 2;
    }
    return width;
}

static int ring_hole(const IconOp *op, int row)
{
    int inner = op->c - 2 * op->d;
    int inner_row = row - op->d;
    if (op->d == 0 || inner <= 0 || inner_row < 0 || inner_row >= inner) {
        return 0;
    }
    return span_width(inner, inner_row);
}

static RowSpan triangle_row(const IconOp *op, int row, int width, int height)
{
    if (op->e == POINT_UP || op->e == POINT_DOWN) {
        int step = op->e == POINT_UP ? row : height - 1 - row;
        int extent = width - 2 * ((width - (2 * step + 1) * width / (2 * height - 1)) / 2);
        return (RowSpan){(width - extent) / 2, extent, 0};
    }
    int reach = min_of(row, height - 1 - row);
    int extent = max_of((2 * reach + 1) * width / height, 1);
    return (RowSpan){op->e == POINT_LEFT ? width - extent : 0, extent, 0};
}

static RowSpan shape_row(const IconOp *op, int row, int width, int height)
{
    if (op->kind == OP_RING) {
        int outer = span_width(width, row);
        return (RowSpan){(width - outer) / 2, outer, ring_hole(op, row)};
    }
    if (op->kind == OP_TRI) {
        return triangle_row(op, row, width, height);
    }
    int edge = (op->e & ROUND) != 0 && (row == 0 || row == height - 1) ? 1 : 0;
    int pen = op->kind == OP_RECT ? op->e & PEN_MASK : height;
    int hole = row >= pen && row < height - pen ? width - 2 * pen : 0;
    return (RowSpan){edge, width - 2 * edge, hole};
}

static void draw_shape(const Pen *pen, const IconOp *op)
{
    bool ring = op->kind == OP_RING;
    int width = ring ? op->c : op->c - op->a + 1;
    int height = ring ? op->c : op->d - op->b + 1;
    for (int row = 0; row < height; row++) {
        RowSpan span = shape_row(op, row, width, height);
        bool top = 2 * row + 1 < height;
        int quadrants = !ring ? TOP : top ? op->e : op->e >> 2;
        int hole = ring && top ? max_of(span.hole, 2 * (op->e >> GAP_SHIFT)) : span.hole;
        int left = op->a + span.start;
        int right = left + span.width - 1;
        int y = op->b + row;
        if ((quadrants & TOP_LEFT) != 0) {
            emit(pen, (Box){left, y, left + (span.width - hole) / 2 - 1, y});
        }
        if ((quadrants & TOP_RIGHT) != 0) {
            emit(pen, (Box){left + (span.width + hole) / 2, y, right, y});
        }
    }
}

static const uint32_t *glyph(IconId icon)
{
    const uint32_t *word = GLYPHS;
    for (int index = ICON_NONE + 1; index < (int)icon; word++) {
        index += (*word & ICON_LAST_BIT) != 0 ? 1 : 0;
    }
    return word;
}

int icon_grid(IconId icon)
{
    return (int)icon >= ICON_FIRST_SMALL ? ICON_SMALL_GRID : ICON_LARGE_GRID;
}

void icon_draw(Canvas *canvas, IconId icon, Rect area, IconInk ink)
{
    Canvas clipped = canvas_clipped(canvas, area);
    if (icon <= ICON_NONE || icon >= ICON_COUNT || rect_is_empty(clipped.clip)) {
        return;
    }
    Pen pen = {.canvas = &clipped, .area = area, .grid = icon_grid(icon), .color = ink.main};
    const uint32_t *word = glyph(icon);
    do {
        IconOp op = decode(*word);
        pen.color = (*word & ICON_ACCENT_BIT) != 0 ? ink.accent : ink.main;
        if (op.kind == OP_LINE) {
            draw_line(&pen, &op);
        } else {
            draw_shape(&pen, &op);
        }
    } while ((*word++ & ICON_LAST_BIT) == 0);
}
