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
    FIRST_SMALL = ICON_PLAY,
    FIRST_NARROW = ICON_BOLT,
    KIND_SHIFT = 29,
    A_SHIFT = 22,
    B_SHIFT = 17,
    C_SHIFT = 12,
    D_SHIFT = 7,
    FIELD_MASK = 31,
    EXTRA_MASK = 127,
    PEN_MASK = 3
};

#define ICON_ACCENT_BIT (1u << 28)
#define ICON_LAST_BIT (1u << 27)
#define ICON_OP(kind, a, b, c, d, e)                                                               \
    (((uint32_t)(kind) << KIND_SHIFT) | ((uint32_t)(a) << A_SHIFT) | ((uint32_t)(b) << B_SHIFT) |  \
     ((uint32_t)(c) << C_SHIFT) | ((uint32_t)(d) << D_SHIFT) | (uint32_t)(e))
#define FILL(x0, y0, x1, y1) ICON_OP(OP_FILL, x0, y0, x1, y1, 0)
#define BLOCK(x0, y0, x1, y1) ICON_OP(OP_FILL, x0, y0, x1, y1, ROUND)
#define LINE(x0, y0, x1, y1) ICON_OP(OP_LINE, x0, y0, x1, y1, 2)
#define THIN(x0, y0, x1, y1) ICON_OP(OP_LINE, x0, y0, x1, y1, 1)
#define THICK(x0, y0, x1, y1) ICON_OP(OP_LINE, x0, y0, x1, y1, 3)
#define BOX(x0, y0, x1, y1) ICON_OP(OP_RECT, x0, y0, x1, y1, 2 | ROUND)
#define RING(x0, y0, size) ICON_OP(OP_RING, x0, y0, size, 2, TOP | BOTTOM)
#define ARC(x0, y0, size, quadrants) ICON_OP(OP_RING, x0, y0, size, 2, quadrants)
#define OPEN_RING(x0, y0, size, gap)                                                               \
    ICON_OP(OP_RING, x0, y0, size, 2, TOP | BOTTOM | (gap) << GAP_SHIFT)
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
#define ALBUM_SMALL_GLYPH LAST(ICON_OP(OP_RING, 0, 0, 14, 5, TOP | BOTTOM))
#define SHUFFLE_SMALL_GLYPH                                                                        \
    THIN(0, 3, 3, 3), THIN(3, 3, 9, 9), TRIANGLE(10, 8, 13, 12, POINT_RIGHT), THIN(0, 10, 3, 10),  \
        THIN(3, 10, 9, 4), LAST(TRIANGLE(10, 1, 13, 5, POINT_RIGHT))
#define BOLT_GLYPH THICK(5, 0, 0, 7), FILL(0, 6, 7, 7), FILL(0, 8, 4, 9), LAST(THICK(6, 6, 1, 12))
#define BLUETOOTH_SMALL_GLYPH                                                                      \
    THIN(4, 0, 4, 13), THIN(4, 0, 8, 4), THIN(8, 4, 1, 11), THIN(4, 13, 8, 9),                     \
        LAST(THIN(8, 9, 1, 2))
#define LOCK_GLYPH BOX(1, 0, 7, 9), BLOCK(0, 6, 8, 13), LAST(ACCENT(FILL(4, 8, 4, 10)))
#define SONG_SMALL_GLYPH DISC(0, 8, 6), LINE(4, 0, 4, 9), LAST(LINE(5, 1, 7, 3))

static const uint32_t GLYPHS[] = {
    MUSIC_GLYPH,       AUDIOBOOK_GLYPH,     PODCAST_GLYPH,     NOW_PLAYING_GLYPH,
    MIXTAPE_GLYPH,     AUDIO_GLYPH,         SETTINGS_GLYPH,    COVER_FLOW_GLYPH,
    ARTIST_GLYPH,      ALBUM_ARTIST_GLYPH,  ALBUM_GLYPH,       GENRE_GLYPH,
    FOLDER_GLYPH,      SONG_GLYPH,          PLAYLIST_GLYPH,    FAVORITE_GLYPH,
    SHUFFLE_GLYPH,     REPEAT_GLYPH,        DISPLAY_GLYPH,     THEME_GLYPH,
    POWER_GLYPH,       SLEEP_GLYPH,         TIMEOUT_GLYPH,     CONTROLS_GLYPH,
    START_GLYPH,       RESCAN_GLYPH,        DIAGNOSTICS_GLYPH, GAIN_GLYPH,
    JACK_GLYPH,        USB_GLYPH,           BLUETOOTH_GLYPH,   KEY_PLUS_GLYPH,
    KEY_MINUS_GLYPH,   KEY_PLAY_GLYPH,      KEY_MENU_GLYPH,    DISC_GLYPH,
    HUB_GLYPH,         PLAY_GLYPH,          PAUSE_GLYPH,       NEXT_GLYPH,
    PREVIOUS_GLYPH,    SEEK_GLYPH,          QUEUE_GLYPH,       SPEAKER_GLYPH,
    ALBUM_SMALL_GLYPH, SHUFFLE_SMALL_GLYPH, BOLT_GLYPH,        BLUETOOTH_SMALL_GLYPH,
    LOCK_GLYPH,        SONG_SMALL_GLYPH,
};

typedef struct Pen {
    Canvas *canvas;
    Point origin;
    int size;
    int grid;
    Color color;
} Pen;

typedef struct Extent {
    int cells;
    int pixels;
} Extent;

typedef struct Shape {
    int kind;
    int extra;
    Point origin;
    int width;
    int height;
    int pen;
    int unit;
} Shape;

typedef struct RowSpan {
    int start;
    int width;
    int hole;
    int sides;
} RowSpan;

typedef struct RowEdges {
    int left;
    int gap_left;
    int gap_right;
    int right;
} RowEdges;

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

static int field(uint32_t word, int shift)
{
    return (int)(word >> shift) & FIELD_MASK;
}

static int extent_start(const Pen *pen, int cells, Extent extent)
{
    return ((2 * cells + extent.cells) * pen->size - (extent.pixels - 1) * pen->grid) /
           (2 * pen->grid);
}

static int stroke(const Pen *pen, int cells)
{
    int width = extent_start(pen, cells, (Extent){0, 0});
    return cells > 0 && width == 0 ? 1 : width;
}

static Point place(const Pen *pen, int column, int row, Extent extent)
{
    return (Point){pen->origin.x + extent_start(pen, column, extent),
                   pen->origin.y + extent_start(pen, row, extent)};
}

static void fill_between(const Pen *pen, Point from, Point to, int size)
{
    Rect area = {min_of(from.x, to.x), min_of(from.y, to.y), magnitude(to.x - from.x) + size,
                 magnitude(to.y - from.y) + size};
    canvas_fill_rect(pen->canvas, area, pen->color);
}

static void draw_slope(const Pen *pen, Point at, Point end, int size)
{
    int dx = magnitude(end.x - at.x);
    int dy = -magnitude(end.y - at.y);
    int error = dx + dy;
    for (Point run = at;; run = at) {
        while (at.x != end.x && 2 * error > dx) {
            error += dy;
            at.x += at.x < end.x ? 1 : -1;
        }
        fill_between(pen, run, at, size);
        if (at.x == end.x && at.y == end.y) {
            return;
        }
        bool across = 2 * error >= dy;
        error += dx + (across ? dy : 0);
        at.x += !across ? 0 : at.x < end.x ? 1 : -1;
        at.y += at.y < end.y ? 1 : -1;
    }
}

static void draw_line(const Pen *pen, uint32_t word)
{
    int cells = (int)word & EXTRA_MASK;
    Extent stamp = {cells, stroke(pen, cells)};
    Point from = place(pen, field(word, A_SHIFT), field(word, B_SHIFT), stamp);
    Point to = place(pen, field(word, C_SHIFT), field(word, D_SHIFT), stamp);
    if (from.x == to.x || from.y == to.y) {
        fill_between(pen, from, to, stamp.pixels);
    } else {
        draw_slope(pen, from, to, stamp.pixels);
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

static int ring_hole(const Shape *shape, int row)
{
    int inner = shape->width - 2 * shape->pen;
    int inner_row = row - shape->pen;
    if (shape->pen == 0 || inner_row < 0 || inner_row >= inner) {
        return 0;
    }
    return span_width(inner, inner_row);
}

static RowSpan ring_row(const Shape *shape, int row)
{
    bool upper = 2 * row + 1 < shape->height;
    int outer = span_width(shape->width, row);
    int gap = upper ? 2 * (shape->extra >> GAP_SHIFT) * shape->unit : 0;
    return (RowSpan){(shape->width - outer) / 2, outer, max_of(ring_hole(shape, row), gap),
                     upper ? shape->extra : shape->extra >> 2};
}

static RowSpan triangle_row(const Shape *shape, int row)
{
    int width = shape->width;
    int height = shape->height;
    if (shape->extra == POINT_UP || shape->extra == POINT_DOWN) {
        int step = shape->extra == POINT_UP ? row : height - 1 - row;
        int extent = width - 2 * ((width - (2 * step + 1) * width / (2 * height - 1)) / 2);
        return (RowSpan){(width - extent) / 2, extent, 0, TOP};
    }
    int reach = min_of(row, height - 1 - row);
    int extent = max_of((2 * reach + 1) * width / height, 1);
    return (RowSpan){shape->extra == POINT_LEFT ? width - extent : 0, extent, 0, TOP};
}

static RowSpan rect_row(const Shape *shape, int row)
{
    int inward = min_of(row, shape->height - 1 - row);
    int inset = (shape->extra & ROUND) != 0 && inward < shape->unit ? shape->unit : 0;
    int pen = shape->kind == OP_RECT ? shape->pen : shape->height;
    int hole = inward >= pen ? shape->width - 2 * pen : 0;
    return (RowSpan){inset, shape->width - 2 * inset, max_of(hole, 0), TOP};
}

static RowEdges row_edges(const Shape *shape, int row)
{
    RowSpan span = shape->kind == OP_RING  ? ring_row(shape, row)
                   : shape->kind == OP_TRI ? triangle_row(shape, row)
                                           : rect_row(shape, row);
    int left = shape->origin.x + span.start;
    int gap_left = left + (span.width - span.hole) / 2;
    int gap_right = left + (span.width + span.hole) / 2;
    return (RowEdges){(span.sides & TOP_LEFT) != 0 ? left : gap_left, gap_left, gap_right,
                      (span.sides & TOP_RIGHT) != 0 ? left + span.width : gap_right};
}

static bool same_row(RowEdges first, RowEdges second)
{
    return first.left == second.left && first.gap_left == second.gap_left &&
           first.gap_right == second.gap_right && first.right == second.right;
}

static void fill_rows(const Pen *pen, RowEdges row, int top, int count)
{
    if (row.gap_left < row.gap_right) {
        Rect before = {row.left, top, row.gap_left - row.left, count};
        canvas_fill_rect(pen->canvas, before, pen->color);
        row.left = row.gap_right;
    }
    canvas_fill_rect(pen->canvas, (Rect){row.left, top, row.right - row.left, count}, pen->color);
}

static void draw_shape(const Pen *pen, const Shape *shape)
{
    RowEdges run = row_edges(shape, 0);
    int start = 0;
    for (int row = 1; row <= shape->height; row++) {
        RowEdges next = row < shape->height ? row_edges(shape, row) : run;
        if (row == shape->height || !same_row(run, next)) {
            fill_rows(pen, run, shape->origin.y + start, row - start);
            run = next;
            start = row;
        }
    }
}

static Shape shape_of(const Pen *pen, uint32_t word)
{
    int kind = (int)(word >> KIND_SHIFT);
    int extra = (int)word & EXTRA_MASK;
    int cells = field(word, C_SHIFT);
    Extent disc = kind == OP_RING ? (Extent){cells, stroke(pen, cells)} : (Extent){0, 0};
    Point from = place(pen, field(word, A_SHIFT), field(word, B_SHIFT), disc);
    Point to = place(pen, cells + 1, field(word, D_SHIFT) + 1, disc);
    return (Shape){.kind = kind,
                   .extra = extra,
                   .origin = from,
                   .width = kind == OP_RING ? disc.pixels : max_of(to.x - from.x, 1),
                   .height = kind == OP_RING ? disc.pixels : max_of(to.y - from.y, 1),
                   .pen = stroke(pen, kind == OP_RING ? field(word, D_SHIFT) : extra & PEN_MASK),
                   .unit = stroke(pen, 1)};
}

static const uint32_t *glyph(IconId icon)
{
    const uint32_t *word = GLYPHS;
    for (int index = ICON_NONE + 1; index < (int)icon; word++) {
        index += (*word & ICON_LAST_BIT) != 0 ? 1 : 0;
    }
    return word;
}

int icon_rows(IconId icon)
{
    return (int)icon >= FIRST_SMALL ? ICON_SMALL_GRID : ICON_LARGE_GRID;
}

int icon_width(IconId icon, int height)
{
    int rows = icon_rows(icon);
    return ((int)icon >= FIRST_NARROW ? ICON_NARROW_COLUMNS : rows) * height / rows;
}

static Pen pen_for(Canvas *canvas, IconId icon, Rect area)
{
    int rows = icon_rows(icon);
    int height = min_of(area.height, area.width * rows / icon_width(icon, rows));
    return (Pen){.canvas = canvas,
                 .origin = {area.x + (area.width - icon_width(icon, height)) / 2,
                            area.y + (area.height - height) / 2},
                 .size = height,
                 .grid = rows};
}

void icon_draw(Canvas *canvas, IconId icon, Rect area, IconInk ink)
{
    Canvas clipped = canvas_clipped(canvas, area);
    if (icon <= ICON_NONE || icon >= ICON_COUNT || rect_is_empty(clipped.clip)) {
        return;
    }
    Pen pen = pen_for(&clipped, icon, area);
    const uint32_t *word = glyph(icon);
    do {
        pen.color = (*word & ICON_ACCENT_BIT) != 0 ? ink.accent : ink.main;
        if (*word >> KIND_SHIFT == OP_LINE) {
            draw_line(&pen, *word);
        } else {
            Shape shape = shape_of(&pen, *word);
            draw_shape(&pen, &shape);
        }
    } while ((*word++ & ICON_LAST_BIT) == 0);
}
