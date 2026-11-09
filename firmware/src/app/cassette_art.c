#include "cassette_art.h"

#include "ui/icons.h"
#include "ui/label.h"

enum {
    EDGE = 2,
    LABEL_INSET = 6,
    LABEL_HEIGHT = 40,
    TEXT_TOP = 7,
    TEXT_HEIGHT = 18,
    WINDOW_LEFT = 21,
    WINDOW_TOP = 26,
    WINDOW_WIDTH = 70,
    WINDOW_HEIGHT = 18,
    REEL_LEFT = 34,
    REEL_RIGHT = 78,
    REEL_MIDDLE = 35,
    PACK_EMPTY = 14,
    PACK_FULL = 30,
    HUB_SIZE = 12,
    HEAD_LEFT = 28,
    HEAD_TOP = 53,
    HEAD_WIDTH = 56,
    HEAD_HEIGHT = 17,
    HOLE_SIZE = 6,
    HOLE_LEFT = 38,
    HOLE_RIGHT = 68,
    HOLE_TOP = 59
};

static void fill_rounded(Canvas *canvas, Rect area, Color color)
{
    canvas_fill_rect(canvas, (Rect){area.x + 1, area.y, area.width - 2, area.height}, color);
    canvas_fill_rect(canvas, (Rect){area.x, area.y + 1, area.width, area.height - 2}, color);
}

int cassette_art_pack_size(uint32_t position_ms, uint32_t duration_ms)
{
    if (duration_ms == 0) {
        return PACK_EMPTY;
    }
    uint32_t played = position_ms < duration_ms ? position_ms : duration_ms;
    return PACK_EMPTY + (int)((uint64_t)played * (PACK_FULL - PACK_EMPTY) / duration_ms);
}

static void draw_disc(Canvas *canvas, IconId shape, Point center, int size, Color color)
{
    icon_draw(canvas, shape, (Rect){center.x - size / 2, center.y - size / 2, size, size},
              (IconInk){color, color});
}

static void draw_reel(Canvas *window, Point center, int pack, const Theme *theme)
{
    draw_disc(window, ICON_DISC, center, pack, theme->colors.scrollbar_thumb);
    draw_disc(window, ICON_HUB, center, HUB_SIZE, theme->colors.header_text);
}

static void draw_window(Canvas *canvas, Rect area, const Theme *theme, CassetteTape tape)
{
    Rect window = {area.x + WINDOW_LEFT, area.y + WINDOW_TOP, WINDOW_WIDTH, WINDOW_HEIGHT};
    fill_rounded(canvas, window, theme->colors.header_background);
    Canvas clipped = canvas_clipped(canvas, window);
    int taken = cassette_art_pack_size(tape.position_ms, tape.duration_ms);
    int left = PACK_EMPTY + PACK_FULL - taken;
    draw_reel(&clipped, (Point){area.x + REEL_LEFT, area.y + REEL_MIDDLE}, left, theme);
    draw_reel(&clipped, (Point){area.x + REEL_RIGHT, area.y + REEL_MIDDLE}, taken, theme);
}

static void draw_label(Canvas *canvas, Rect area, const Theme *theme, const char *text)
{
    Rect label = {area.x + LABEL_INSET, area.y + LABEL_INSET, area.width - 2 * LABEL_INSET,
                  LABEL_HEIGHT};
    fill_rounded(canvas, label, theme->colors.selection_background);
    TextStyle ink = {.font = theme->fonts.small, .color = theme->colors.selection_text};
    Rect line = {label.x + LABEL_INSET, area.y + TEXT_TOP, label.width - 2 * LABEL_INSET,
                 TEXT_HEIGHT};
    label_draw(canvas, ink, (Label){label_centered_area(ink.font, line, text), text});
}

static void draw_head(Canvas *canvas, Rect area, const Theme *theme)
{
    fill_rounded(canvas, (Rect){area.x + HEAD_LEFT, area.y + HEAD_TOP, HEAD_WIDTH, HEAD_HEIGHT},
                 theme->colors.header_background);
    draw_disc(canvas, ICON_DISC, (Point){area.x + HOLE_LEFT, area.y + HOLE_TOP}, HOLE_SIZE,
              theme->colors.header_top);
    draw_disc(canvas, ICON_DISC, (Point){area.x + HOLE_RIGHT, area.y + HOLE_TOP}, HOLE_SIZE,
              theme->colors.header_top);
}

void cassette_art_draw(Canvas *canvas, Rect area, const Theme *theme, CassetteTape tape)
{
    if (rect_is_empty(rect_intersection(canvas->clip, area))) {
        return;
    }
    Canvas clipped = canvas_clipped(canvas, area);
    fill_rounded(&clipped, area, theme->colors.text_tertiary);
    fill_rounded(
        &clipped,
        (Rect){area.x + EDGE, area.y + EDGE, area.width - 2 * EDGE, area.height - 2 * EDGE},
        theme->colors.header_top);
    draw_label(&clipped, area, theme, tape.label);
    draw_window(&clipped, area, theme, tape);
    draw_head(&clipped, area, theme);
}
