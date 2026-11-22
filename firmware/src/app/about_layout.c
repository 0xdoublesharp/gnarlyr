#include "about_layout.h"

#include <stdbool.h>
#include <stddef.h>

#include "core/i18n.h"
#include "gfx/font.h"
#include "gfx/text.h"
#include "ui/brand_mark.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_view.h"
#include "ui/theme.h"

enum {
    MARK_LINE_SHARE = 1,
    MARK_LINE_PARTS = 1
};

enum {
    NOTICE_BYTES = 160,
    NOTICE_LINES = 4
};

static const uint32_t *names_needing_fonts;

static bool row_wraps(const ListView *list, int index, int width)
{
    const ListModel *model = list->model;
    const Theme *theme = list->theme;
    const char *detail = model->detail(model->context, index);
    if (detail == NULL) {
        return false;
    }
    if (model->opens(model->context, index)) {
        width -= list_view_chevron_width(theme) + theme->metrics.detail_gap;
    }
    return text_width(theme->fonts.title, model->label(model->context, index)) +
               theme->metrics.detail_gap + text_width(theme->fonts.small, detail) >
           width;
}

static int height_at(const ListView *list, int index, int width)
{
    int under = font_line_height(list->theme->fonts.small) + LIST_VIEW_DETAIL_LINE_GAP;
    return list_view_row_height(list) + (row_wraps(list, index, width) ? under : 0);
}

static int rows_height(const ListView *list, int first, int last, int width)
{
    int height = 0;
    for (int index = first; index <= last; index++) {
        height += height_at(list, index, width);
    }
    return height;
}

static int bar_width(const Theme *theme)
{
    return theme->metrics.scrollbar_width + theme->metrics.scrollbar_inset;
}

static int text_width_of(const MenuScreen *menu, Rect content)
{
    const ListView *list = &menu->list;
    int count = menu->config.item_count;
    int wide = content.width - 2 * list->theme->metrics.padding;
    bool scrolls = rows_height(list, 0, count - 1, wide) > content.height;
    return scrolls ? wide - bar_width(list->theme) : wide;
}

static bool any_wraps(const ListView *list, int count, int width)
{
    for (int index = 0; index < count; index++) {
        if (row_wraps(list, index, width)) {
            return true;
        }
    }
    return false;
}

static int last_top(const ListView *list, int count, int width, int room)
{
    int top = count - 1;
    while (top > 0 && rows_height(list, top - 1, count - 1, width) <= room) {
        top--;
    }
    return top;
}

static void draw_rows(MenuScreen *menu, Canvas *canvas, Rect rows, int width)
{
    ListView *list = &menu->list;
    int bottom = rows.y + rows.height;
    for (int index = list->top; index < menu->config.item_count; index++) {
        int height = height_at(list, index, width);
        if (rows.y + height > bottom) {
            break;
        }
        list_view_draw_row(list, canvas, index, (Rect){rows.x, rows.y, rows.width, height},
                           menu->now_ms);
        rows.y += height;
    }
}

AboutBand about_layout_band(const Theme *theme, const char *name)
{
    int pad = theme->metrics.padding;
    int line = font_line_height(theme->fonts.title);
    int size = line * MARK_LINE_SHARE / MARK_LINE_PARTS;
    Rect content = theme_content_area(theme);
    Rect band = {content.x, content.y, content.width, size + pad};
    int group = size + pad + text_width(theme->fonts.title, name);
    int left = band.x + (band.width - group) / 2;
    left = left > pad ? left : pad;
    Rect mark = {left, band.y + (band.height - size) / 2, size, size};
    int name_x = mark.x + size + pad;
    return (AboutBand){
        band,
        mark,
        {name_x, band.y + (band.height - line) / 2, band.x + band.width - pad - name_x, line}};
}

static Rect draw_band(Canvas *canvas, const Theme *theme)
{
    const char *name = FIRMWARE_NAME;
    AboutBand about = about_layout_band(theme, name);
    theme_background_draw(theme, canvas, THEME_ART_LIST, about.band);
    Color behind = theme->colors.background;
    brand_mark_draw(canvas,
                    (BrandMark){about.mark, behind, UINT8_MAX, brand_mark_needs_tile(behind)});
    label_draw(canvas, (TextStyle){theme->fonts.title, theme->colors.text, false},
               (Label){about.name, name});
    Rect content = theme_content_area(theme);
    return (Rect){content.x, content.y + about.band.height, content.width,
                  content.height - about.band.height};
}

static Rect draw_notice(Canvas *canvas, const Theme *theme, Rect content)
{
    uint32_t names = names_needing_fonts != NULL ? *names_needing_fonts : 0;
    if (names == 0) {
        return content;
    }
    char notice[NOTICE_BYTES];
    StringBuilder text = string_builder_make(notice, sizeof notice);
    i18n_format_counts(&text, names == 1 ? STR_NAMES_NEED_FONTS_ONE : STR_NAMES_NEED_FONTS, names,
                       0);
    const Font *font = theme->fonts.small;
    int pad = theme->metrics.padding;
    Rect lines = {content.x + 2 * pad, 0, content.width - 3 * pad, 0};
    int count = label_wrap_count(font, notice, lines.width);
    lines.height = (count < NOTICE_LINES ? count : NOTICE_LINES) * font_line_height(font);
    content.height -= lines.height + pad;
    lines.y = content.y + content.height + pad / 2;
    theme_background_draw(theme, canvas, THEME_ART_LIST,
                          (Rect){content.x, lines.y - pad / 2, content.width, lines.height + pad});
    canvas_fill_rect(canvas, (Rect){content.x + pad, lines.y + pad / 2, pad / 2, pad / 2},
                     theme->colors.accent_alt);
    label_draw_paragraph(canvas, (TextStyle){.font = font, .color = theme->colors.text_secondary},
                         (Label){lines, notice});
    return content;
}

static void draw(void *context, Canvas *canvas)
{
    MenuScreen *menu = context;
    ListView *list = &menu->list;
    const Theme *theme = menu->config.theme;
    header_draw(canvas, theme, menu->config.title);
    Rect content = draw_notice(canvas, theme, draw_band(canvas, theme));
    int count = menu->config.item_count;
    int width = text_width_of(menu, content);
    if (!theme_is_portrait(theme) || !any_wraps(list, count, width)) {
        list_view_fit_rows(list, list_view_rows_in(list, content.height));
        list_view_draw(list, canvas, content, menu->now_ms);
        return;
    }
    int last = last_top(list, count, width, content.height);
    list->top = list->top > last ? last : list->top;
    while (list->top < list->selected &&
           rows_height(list, list->top, list->selected, width) > content.height) {
        list->top++;
    }
    theme_background_draw(theme, canvas, THEME_ART_LIST, content);
    Rect rows = content;
    if (width < content.width - 2 * theme->metrics.padding) {
        rows.width -= bar_width(theme);
        list_view_draw_scrollbar(list, canvas, content, last);
    }
    draw_rows(menu, canvas, rows, width);
}

void about_layout_count_names(const uint32_t *names)
{
    names_needing_fonts = names;
}

void about_layout_attach(MenuScreen *menu)
{
    menu->model.wraps = false;
    menu->screen.draw = draw;
}
