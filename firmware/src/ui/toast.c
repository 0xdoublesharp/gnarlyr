#include "ui/toast.h"

#include "core/deadline.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "ui/label.h"

enum {
    TOAST_BOTTOM_MARGIN = 10,
    BORDER = 1
};

CORE_SEAM void toast_show(Toast *toast, const char *text, uint32_t now_ms)
{
    StringBuilder copy = string_builder_make(toast->text, sizeof toast->text);
    string_builder_append(&copy, text);
    toast->until_ms = now_ms + TOAST_DURATION_MS;
    toast->visible = true;
}

void toast_update(Toast *toast, uint32_t now_ms)
{
    if (toast->visible && deadline_reached(now_ms, toast->until_ms)) {
        toast->visible = false;
    }
}

static int text_room(const Theme *theme)
{
    return theme->metrics.screen_width - 2 * theme->metrics.padding;
}

static const Font *toast_font(const Toast *toast, const Theme *theme)
{
    return text_width(theme->fonts.body, toast->text) <= text_room(theme) ? theme->fonts.body
                                                                          : theme->fonts.small;
}

static int toast_lines(const Toast *toast, const Theme *theme)
{
    int lines = label_wrap_count(toast_font(toast, theme), toast->text, text_room(theme));
    return lines < TOAST_MAX_LINES ? lines : TOAST_MAX_LINES;
}

Rect toast_area(const Toast *toast, const Theme *theme)
{
    const ThemeMetrics *metrics = &theme->metrics;
    const Font *font = toast_font(toast, theme);
    int lines = toast_lines(toast, theme);
    int width = text_width(font, toast->text) + 2 * metrics->padding;
    width = lines == 1 && width < metrics->screen_width ? width : metrics->screen_width;
    int height = metrics->row_height + (lines - 1) * font_line_height(font);
    height = height < metrics->screen_height ? height : metrics->screen_height;
    int bottom_margin =
        metrics->screen_height > height + TOAST_BOTTOM_MARGIN ? TOAST_BOTTOM_MARGIN : 0;
    return (Rect){(metrics->screen_width - width) / 2,
                  metrics->screen_height - height - bottom_margin, width, height};
}

void toast_draw(const Toast *toast, Canvas *canvas, const Theme *theme)
{
    if (!toast->visible) {
        return;
    }
    Rect area = toast_area(toast, theme);
    Rect inner = {area.x + BORDER, area.y + BORDER, area.width - 2 * BORDER,
                  area.height - 2 * BORDER};
    Rect text = {area.x + theme->metrics.padding, inner.y, area.width - 2 * theme->metrics.padding,
                 inner.height};
    TextStyle style = {.font = toast_font(toast, theme), .color = theme->colors.header_text};
    canvas_fill_rect(canvas, area, theme->colors.divider);
    canvas_fill_rect(canvas, inner, theme->colors.header_background);
    label_draw_wrapped(canvas, style, (Label){.area = text, .text = toast->text});
}
