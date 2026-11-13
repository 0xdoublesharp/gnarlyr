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

static bool two_lines(const Theme *theme)
{
    return theme->metrics.status_height > 0 &&
           theme->metrics.header_height > theme->metrics.status_height;
}

__attribute__((noinline)) static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

bool header_shows_title(const Theme *theme, const char *title)
{
    return title != NULL && !(two_lines(theme) && same_text(title, FIRMWARE_SHORT_NAME));
}

void header_draw_app_name(Canvas *canvas, const Theme *theme)
{
    if (!two_lines(theme)) {
        return;
    }
    int inset = theme->metrics.padding;
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.header_text};
    Rect line = {inset, 0, theme->metrics.screen_width - 2 * inset, theme->metrics.status_height};
    label_draw(canvas, style, (Label){.area = line, .text = FIRMWARE_SHORT_NAME});
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
    header_draw_app_name(canvas, theme);
    if (title_bounds.height > 0 && header_shows_title(theme, title)) {
        Rect title_area = header_title_area(theme, style.font, title);
        label_draw(canvas, style, (Label){.area = title_area, .text = title});
    }
}
