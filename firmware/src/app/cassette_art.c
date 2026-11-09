#include "cassette_art.h"

#include "ui/label.h"

static void disk(Canvas *canvas, Point center, int radius, Color color)
{
    for (int y = -radius; y <= radius; y++) {
        int half = radius;
        while (half * half + y * y > radius * radius) {
            half--;
        }
        canvas_fill_rect(canvas, (Rect){center.x - half, center.y + y, 2 * half + 1, 1}, color);
    }
}

static void reel(Canvas *canvas, Point center, const Theme *theme, unsigned phase)
{
    disk(canvas, center, 17, theme->colors.background);
    disk(canvas, center, 14, theme->colors.selection_background);
    Color spoke = theme->colors.header_background;
    if (phase == 0) {
        canvas_fill_rect(canvas, (Rect){center.x - 1, center.y - 13, 3, 27}, spoke);
        canvas_fill_rect(canvas, (Rect){center.x - 13, center.y - 1, 27, 3}, spoke);
    } else {
        for (int offset = -9; offset <= 9; offset++) {
            canvas_fill_rect(canvas, (Rect){center.x + offset, center.y + offset, 3, 2}, spoke);
            canvas_fill_rect(canvas, (Rect){center.x - offset, center.y + offset, 3, 2}, spoke);
        }
    }
    disk(canvas, center, 5, theme->colors.header_background);
    disk(canvas, center, 2, theme->colors.background);
}

static void tape_labels(Canvas *canvas, Rect area, const Theme *theme)
{
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text};
    Rect label = {area.x + 8, area.y + 5, area.width - 16, 22};
    canvas_fill_rect(canvas, label, theme->colors.selection_background);
    label_draw(canvas, style,
               (Label){label_centered_area(style.font, label, "MIXTAPE"), "MIXTAPE"});
    label_draw(canvas, style, (Label){{area.x + 8, area.y + 82, 52, 18}, "SIDE A"});
    label_draw(canvas, style, (Label){{area.x + area.width - 43, area.y + 82, 36, 18}, "HI-FI"});
}

void cassette_art_draw(Canvas *canvas, Rect area, const Theme *theme, uint32_t position_ms)
{
    if (area.width < 112 || area.height < 104 ||
        rect_is_empty(rect_intersection(canvas->clip, area))) {
        return;
    }
    Canvas clipped = canvas_clipped(canvas, area);
    canvas_fill_rect(&clipped, area, theme->colors.divider);
    canvas_fill_rect(&clipped, (Rect){area.x + 2, area.y + 2, area.width - 4, area.height - 4},
                     theme->colors.background);
    Rect window = {area.x + 8, area.y + 32, area.width - 16, 44};
    canvas_fill_rect(&clipped, window, theme->colors.header_background);
    canvas_fill_rect(&clipped, (Rect){area.x + 30, area.y + 52, area.width - 60, 4},
                     theme->colors.divider);
    unsigned phase = position_ms / 500 % 2;
    reel(&clipped, (Point){area.x + 30, area.y + 54}, theme, phase);
    reel(&clipped, (Point){area.x + area.width - 30, area.y + 54}, theme, phase);
    tape_labels(&clipped, area, theme);
}
