#include "ui/header.h"

#include "ui/label.h"

enum {
    DIVIDER_HEIGHT = 1
};

static bool has_room(const Theme *theme)
{
    return theme->header_room.right > theme->header_room.left;
}

Rect header_title_bounds(const Theme *theme)
{
    int height = theme->metrics.header_height - DIVIDER_HEIGHT;
    if (has_room(theme)) {
        const HeaderRoom *room = &theme->header_room;
        return (Rect){room->left, 0, room->right - room->left, height};
    }
    int inset = theme->metrics.header_inset > theme->metrics.padding ? theme->metrics.header_inset
                                                                     : theme->metrics.padding;
    return (Rect){inset, 0, theme->metrics.screen_width - 2 * inset, height};
}

Rect header_title_area(const Theme *theme, const Font *font, const char *title)
{
    Rect bounds = header_title_bounds(theme);
    Rect area = label_centered_area(font, bounds, title);
    if (has_room(theme)) {
        area.x = bounds.x;
    }
    return area;
}

static void draw_title(Canvas *canvas, const Theme *theme, TextStyle style, const char *title)
{
    Rect area = header_title_area(theme, style.font, title);
    int shift = theme->header_room.shift;
    if (shift > 0) {
        Rect bounds = header_title_bounds(theme);
        area.width = bounds.x + bounds.width - area.x;
        label_draw_scrolling(canvas, style, (Label){area, title},
                             (Marquee){.offset = shift, .gap = theme->marquee.gap_px});
        return;
    }
    label_draw(canvas, style, (Label){.area = area, .text = title});
}

void header_draw(Canvas *canvas, const Theme *theme, const char *title)
{
    Rect area = theme_header_area(theme);
    Rect divider = {area.x, area.y + area.height - DIVIDER_HEIGHT, area.width, DIVIDER_HEIGHT};
    TextStyle style = {.font = theme->fonts.header, .color = theme->colors.header_text};
    Rect body = {area.x, area.y, area.width, area.height - DIVIDER_HEIGHT};
    canvas_fill_rect(canvas, body, theme->colors.header_background);
    canvas_fill_rect(canvas, divider,
                     theme->header_rule ? theme->colors.accent : theme->colors.divider);
    if (body.height > 0 && title != NULL) {
        draw_title(canvas, theme, style, title);
    }
}
