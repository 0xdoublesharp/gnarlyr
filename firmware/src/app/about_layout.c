#include "about_layout.h"

#include <stdbool.h>
#include <stddef.h>

#include "gfx/font.h"
#include "gfx/text.h"
#include "ui/header.h"
#include "ui/list_view.h"
#include "ui/theme.h"

static void (*menu_draw)(void *context, Canvas *canvas);

static bool row_wraps(const ListView *list, int index, int width)
{
    const ListModel *model = list->model;
    const Theme *theme = list->theme;
    const char *detail = model->detail(model->context, index);
    if (detail == NULL) {
        return false;
    }
    if (model->opens(model->context, index)) {
        width -= text_width(theme->fonts.title, LIST_VIEW_CHEVRON) + theme->metrics.detail_gap;
    }
    return text_width(theme->fonts.body, model->label(model->context, index)) +
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

static void draw(void *context, Canvas *canvas)
{
    MenuScreen *menu = context;
    ListView *list = &menu->list;
    const Theme *theme = menu->config.theme;
    Rect content = theme_content_area(theme);
    int count = menu->config.item_count;
    int width = text_width_of(menu, content);
    if (!theme_is_portrait(theme) || !any_wraps(list, count, width)) {
        menu_draw(context, canvas);
        return;
    }
    int last = last_top(list, count, width, content.height);
    list->top = list->top > last ? last : list->top;
    while (list->top < list->selected &&
           rows_height(list, list->top, list->selected, width) > content.height) {
        list->top++;
    }
    header_draw(canvas, theme, menu->config.title);
    theme_background_draw(theme, canvas, THEME_ART_LIST, content);
    Rect rows = content;
    if (width < content.width - 2 * theme->metrics.padding) {
        rows.width -= bar_width(theme);
        list_view_draw_scrollbar(list, canvas, content, last);
    }
    draw_rows(menu, canvas, rows, width);
}

void about_layout_attach(MenuScreen *menu)
{
    menu_draw = menu->screen.draw;
    menu->screen.draw = draw;
}
