#include "ui/theme.h"

#include <stddef.h>

Rect theme_header_area(const Theme *theme)
{
    return (Rect){0, 0, theme->metrics.screen_width, theme->metrics.header_height};
}

Rect theme_content_area(const Theme *theme)
{
    const ThemeMetrics *metrics = &theme->metrics;
    return (Rect){0, metrics->header_height, metrics->screen_width,
                  metrics->screen_height - metrics->header_height};
}

int theme_visible_rows(const Theme *theme)
{
    int rows = theme_content_area(theme).height / theme->metrics.row_height;
    return rows < THEME_MAX_ROWS ? rows : THEME_MAX_ROWS;
}

bool theme_is_portrait(const Theme *theme)
{
    return theme->metrics.screen_height > theme->metrics.screen_width;
}

Rect theme_title_area(const Theme *theme)
{
    const ThemeMetrics *metrics = &theme->metrics;
    return (Rect){0, metrics->status_height, metrics->screen_width,
                  metrics->header_height - metrics->status_height};
}

bool theme_art_draw(const Theme *theme, Canvas *canvas, int slot, Rect area)
{
    return theme->draw_art != NULL && theme->draw_art(theme, canvas, slot, area);
}

void theme_background_draw(const Theme *theme, Canvas *canvas, int slot, Rect area)
{
    if (!theme_art_draw(theme, canvas, slot, area)) {
        canvas_fill_rect(canvas, area, theme->colors.background);
    }
}
