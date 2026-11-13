#include "ui/header.h"

#include "ui/label.h"

enum {
    DIVIDER_HEIGHT = 1
};

Rect header_title_bounds(const Theme *theme)
{
    int inset = theme->metrics.header_inset > theme->metrics.padding ? theme->metrics.header_inset
                                                                     : theme->metrics.padding;
    Rect row = theme_title_area(theme);
    return (Rect){row.x + inset, row.y, row.width - 2 * inset, row.height - DIVIDER_HEIGHT};
}

Rect header_title_area(const Theme *theme, const Font *font, const char *title)
{
    Rect bounds = header_title_bounds(theme);
    Rect area = label_centered_area(font, bounds, title);
    if (theme->metrics.status_height > 0) {
        area.x = bounds.x;
    }
    return area;
}

void header_draw(Canvas *canvas, const Theme *theme, const char *title)
{
    Rect area = theme_header_area(theme);
    Rect divider = {area.x, area.y + area.height - DIVIDER_HEIGHT, area.width, DIVIDER_HEIGHT};
    Rect title_bounds = header_title_bounds(theme);
    TextStyle style = {.font = theme->fonts.header, .color = theme->colors.header_text};
    Rect body = {area.x, area.y, area.width, area.height - DIVIDER_HEIGHT};
    canvas_fill_rect(canvas, body, theme->colors.header_background);
    canvas_fill_rect(canvas, divider,
                     theme->header_rule ? theme->colors.accent : theme->colors.divider);
    if (title_bounds.height > 0) {
        Rect title_area = header_title_area(theme, style.font, title);
        label_draw(canvas, style, (Label){.area = title_area, .text = title});
    }
}
