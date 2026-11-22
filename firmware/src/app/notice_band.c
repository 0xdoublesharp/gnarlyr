#include "notice_band.h"

#include "gfx/font.h"
#include "ui/label.h"

enum {
    DOT = 6,
    DOT_GAP = 8,
    BAND_GAP = 4
};

static int text_left(const Theme *theme)
{
    return theme->metrics.padding + DOT + DOT_GAP;
}

int notice_band_height(const Theme *theme, const char *text, int width)
{
    const Font *font = theme->fonts.small;
    int lines = label_wrap_count(font, text, width - text_left(theme) - theme->metrics.padding);
    lines = lines < 1 ? 1 : lines > NOTICE_BAND_LINES ? NOTICE_BAND_LINES : lines;
    return lines * font_line_height(font) + 2 * BAND_GAP;
}

void notice_band_draw(Canvas *canvas, const Theme *theme, Rect area, const char *text)
{
    const Font *font = theme->fonts.small;
    int left = text_left(theme);
    canvas_fill_rect(canvas, (Rect){area.x, area.y, area.width, area.height},
                     theme->colors.background);
    canvas_fill_rect(canvas, (Rect){area.x, area.y, area.width, 1}, theme->colors.divider);
    int dot_y = area.y + BAND_GAP + (font_line_height(font) - DOT) / 2;
    canvas_fill_rect(canvas, (Rect){area.x + theme->metrics.padding, dot_y, DOT, DOT},
                     theme->colors.accent_alt);
    Rect lines = {area.x + left, area.y + BAND_GAP, area.width - left - theme->metrics.padding,
                  area.height - 2 * BAND_GAP};
    label_draw_paragraph(canvas, (TextStyle){.font = font, .color = theme->colors.text_secondary},
                         (Label){lines, text});
}
