#include "ui/list_view.h"

#include <stddef.h>

#include "core/seam.h"
#include "gfx/text.h"
#include "ui/label.h"

enum {
    MIN_THUMB_HEIGHT = 6,
    NARROW_DETAIL_SHARE = 40,
    PERCENT = 100,
    ICON_BOX = 20,
    ICON_MIN_GAP = 4,
    DETAIL_LINE_GAP = 2
};

static const char CHEVRON[] = "\xE2\x80\xBA";
static const char SECTION_LABELS[][2] = {"#", "A", "B", "C", "D", "E", "F", "G", "H", "I",
                                         "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
                                         "T", "U", "V", "W", "X", "Y", "Z", "#"};

typedef struct Row {
    int index;
    Rect area;
    Rect text;
    bool selected;
} Row;

static int item_count(const ListView *view)
{
    return view->model->count(view->model->context);
}

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

static bool details_below(const ListView *view)
{
    return view->model->detail_below && theme_is_portrait(view->theme);
}

int list_view_row_height(const ListView *view)
{
    return view->theme->metrics.row_height +
           (details_below(view) ? font_line_height(view->theme->fonts.small) + DETAIL_LINE_GAP : 0);
}

int list_view_rows_in(const ListView *view, int height)
{
    return min_of(THEME_MAX_ROWS, max_of(0, height) / list_view_row_height(view));
}

static void keep_selection_visible(ListView *view)
{
    if (view->selected < view->top) {
        view->top = view->selected;
    } else if (view->selected >= view->top + view->visible_rows) {
        view->top = view->selected - view->visible_rows + 1;
    }
}

static int wrapped_target(int target, int count, bool wrap)
{
    if (target < 0) {
        return wrap ? count - 1 : 0;
    }
    if (target >= count) {
        return wrap ? 0 : count - 1;
    }
    return target;
}

void list_view_init(ListView *view, const ListModel *model, const Theme *theme, uint32_t now_ms)
{
    *view = (ListView){
        .model = model,
        .theme = theme,
        .visible_rows = 0,
        .selection_changed_ms = now_ms,
    };
    view->visible_rows = list_view_rows_in(view, theme_content_area(theme).height);
}

void list_view_move(ListView *view, int delta, bool wrap, uint32_t now_ms)
{
    int count = item_count(view);
    if (count == 0) {
        return;
    }
    int target = wrapped_target(view->selected + delta, count, wrap);
    if (target == view->selected) {
        return;
    }
    view->selected = target;
    view->selection_changed_ms = now_ms;
    keep_selection_visible(view);
}

CORE_SEAM void list_view_start_scroll(ListView *view, int direction, uint32_t now_ms)
{
    view->scroll_started_ms = now_ms;
    list_view_move(view, direction, true, now_ms);
}

static int section_at(const ListView *view, int index)
{
    int section = view->model->section(view->model->context, index);
    return view->model->section_reverse ? LIST_SECTION_AFTER_LETTERS - section : section;
}

static int first_of_section_at_least(const ListView *view, int section, int low, int high)
{
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (section_at(view, middle) >= section) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return low;
}

static int section_jump_target(const ListView *view, int direction, ListSections sections)
{
    int count = sections.end;
    int current = view->selected;
    int section = section_at(view, current);
    if (direction > 0) {
        return first_of_section_at_least(view, section + 1, current + 1, count);
    }
    int start = first_of_section_at_least(view, section, sections.first, current);
    if (current == sections.first) {
        return max_of(0, current - 1);
    }
    if (start < current) {
        return start;
    }
    return first_of_section_at_least(view, section_at(view, current - 1), sections.first,
                                     current - 1);
}

CORE_SEAM void list_view_keep_scrolling(ListView *view, int direction, uint32_t now_ms)
{
    uint32_t held_ms = now_ms - view->scroll_started_ms;
    if (held_ms < LIST_FAST_SCROLL_AFTER_MS || item_count(view) <= view->visible_rows) {
        list_view_move(view, direction, false, now_ms);
        return;
    }
    ListSections sections = view->model->sections != NULL
                                ? view->model->sections(view->model->context, view->selected)
                                : (ListSections){0, item_count(view)};
    bool sectioned = view->model->section != NULL && view->selected >= sections.first &&
                     view->selected < sections.end;
    int step =
        held_ms < LIST_SECTION_JUMP_AFTER_MS ? LIST_FAST_SCROLL_ROWS : LIST_FASTEST_SCROLL_ROWS;
    if (sectioned) {
        view->section_shown_until_ms = now_ms + LIST_SECTION_SHOWN_MS;
        int boundary = section_jump_target(view, direction, sections);
        int target = view->selected + direction * step;
        if (held_ms >= LIST_SECTION_JUMP_AFTER_MS ||
            (direction > 0 ? target > boundary : target < boundary)) {
            target = boundary;
        }
        list_view_select(view, target, now_ms);
    } else {
        view->section_shown_until_ms = 0;
        list_view_move(view, direction * step, false, now_ms);
    }
}

int list_section_of_key(const char *key)
{
    unsigned char first = (unsigned char)key[0];
    if (first < 'a') {
        return LIST_SECTION_BEFORE_LETTERS;
    }
    if (first > 'z') {
        return LIST_SECTION_AFTER_LETTERS;
    }
    return LIST_SECTION_FIRST_LETTER + (first - 'a');
}

const char *list_section_label(int section)
{
    if (section < LIST_SECTION_BEFORE_LETTERS || section > LIST_SECTION_AFTER_LETTERS) {
        return SECTION_LABELS[LIST_SECTION_BEFORE_LETTERS];
    }
    return SECTION_LABELS[section];
}

void list_view_select(ListView *view, int index, uint32_t now_ms)
{
    int count = item_count(view);
    if (count == 0) {
        return;
    }
    int target = index < 0 ? 0 : index >= count ? count - 1 : index;
    view->selected = target;
    view->selection_changed_ms = now_ms;
    keep_selection_visible(view);
}

void list_view_fit(ListView *view)
{
    list_view_fit_rows(view, list_view_rows_in(view, theme_content_area(view->theme).height));
}

void list_view_fit_rows(ListView *view, int rows)
{
    int count = item_count(view);
    view->visible_rows = rows;
    if (view->top > count - view->visible_rows) {
        view->top = max_of(0, count - view->visible_rows);
    }
    keep_selection_visible(view);
}

void list_view_restart_marquee(ListView *view, uint32_t now_ms)
{
    view->selection_changed_ms = now_ms;
}

static TextStyle row_style(const ListView *view, bool selected, Color normal)
{
    Color color = selected ? view->theme->colors.selection_text : normal;
    return (TextStyle){.font = view->theme->fonts.body, .color = color};
}

static Row draw_icon(const ListView *view, Canvas *canvas, Row row)
{
    const ListModel *model = view->model;
    if (model->icon == NULL) {
        return row;
    }
    int size = min_of(ICON_BOX, min_of(row.text.width, row.text.height));
    int gap = max_of(ICON_MIN_GAP, view->theme->metrics.padding - ICON_MIN_GAP);
    Rect box = {row.text.x, row.text.y + (row.text.height - size) / 2, size, size};
    Color main = row.selected ? view->theme->colors.selection_text : view->theme->colors.text;
    Color accent = row.selected ? main : view->theme->colors.accent;
    Canvas clipped = canvas_clipped(canvas, row.area);
    IconId icon = model->icon(model->context, row.index);
    if (view->background != THEME_ART_HOME ||
        !theme_art_draw(view->theme, &clipped, THEME_ART_ICON + (int)icon, box)) {
        icon_draw(&clipped, icon, box, (IconInk){main, accent});
    }
    int taken = min_of(max_of(0, row.text.width), max_of(0, size + gap));
    row.text.x += taken;
    row.text.width -= taken;
    return row;
}

static Row draw_chevron(const ListView *view, Canvas *canvas, Row row)
{
    const ListModel *model = view->model;
    if (model->opens == NULL || !model->opens(model->context, row.index)) {
        return row;
    }
    Color color = row.selected ? view->theme->colors.selection_text : view->theme->colors.chevron;
    TextStyle style = {.font = view->theme->fonts.title, .color = color};
    int width = text_width(style.font, CHEVRON);
    Rect area = {row.text.x + row.text.width - width, row.text.y, width, row.text.height};
    label_draw(canvas, style, (Label){.area = area, .text = CHEVRON});
    row.text.width -= width + view->theme->metrics.detail_gap;
    if (model->icon != NULL) {
        row.text.width = max_of(0, row.text.width);
    }
    return row;
}

static Row draw_detail(const ListView *view, Canvas *canvas, Row row)
{
    const ListModel *model = view->model;
    const char *detail = model->detail != NULL ? model->detail(model->context, row.index) : NULL;
    if (detail == NULL) {
        return row;
    }
    TextStyle style = row_style(view, row.selected, view->theme->colors.text_secondary);
    bool narrow = theme_is_portrait(view->theme);
    if (narrow) {
        style.font = view->theme->fonts.small;
    }
    int width = text_width(style.font, detail);
    if (model->detail_max_width > 0 && width > model->detail_max_width) {
        width = model->detail_max_width;
    }
    if (narrow) {
        int label_width =
            text_width(view->theme->fonts.body, model->label(model->context, row.index));
        int room = row.text.width - label_width - view->theme->metrics.detail_gap;
        int smallest = row.text.width * NARROW_DETAIL_SHARE / PERCENT;
        width = min_of(width, max_of(room, smallest));
    }
    if (model->icon != NULL) {
        width = min_of(width, max_of(0, row.text.width - view->theme->metrics.detail_gap));
    }
    Rect area = {row.text.x + row.text.width - width, row.text.y, width, row.text.height};
    label_draw(canvas, style, (Label){.area = area, .text = detail});
    row.text.width -= width + view->theme->metrics.detail_gap;
    return row;
}

static void draw_detail_below(const ListView *view, Canvas *canvas, Row row)
{
    const ListModel *model = view->model;
    const Theme *theme = view->theme;
    TextStyle style = {.font = theme->fonts.small,
                       .color = row.selected ? theme->colors.selection_text
                                             : theme->colors.text_secondary};
    Rect area = {row.text.x, row.area.y + theme->metrics.row_height + DETAIL_LINE_GAP,
                 row.text.width, font_line_height(style.font)};
    if (model->opens != NULL && model->opens(model->context, row.index)) {
        int width = text_width(style.font, CHEVRON);
        Rect arrow = {area.x + area.width - width, area.y, width, area.height};
        label_draw(canvas, style, (Label){arrow, CHEVRON});
        area.width = max_of(0, area.width - width - theme->metrics.detail_gap);
    }
    const char *detail = model->detail != NULL ? model->detail(model->context, row.index) : NULL;
    if (detail != NULL) {
        label_draw(canvas, style, (Label){area, detail});
    }
}

static void draw_label(const ListView *view, Canvas *canvas, Row row, uint32_t now_ms)
{
    if (row.text.width <= 0) {
        return;
    }
    const char *text = view->model->label(view->model->context, row.index);
    TextStyle style = row_style(view, row.selected, view->theme->colors.text);
    Label label = {.area = row.text, .text = text};
    int width = text_width(style.font, text);
    if (!row.selected || width <= row.text.width) {
        label_draw(canvas, style, label);
        return;
    }
    MarqueeTiming timing = view->theme->marquee;
    int offset = marquee_offset(timing, width, row.text.width, now_ms - view->selection_changed_ms);
    label_draw_scrolling(canvas, style, label, (Marquee){.offset = offset, .gap = timing.gap_px});
}

static void draw_row(const ListView *view, Canvas *canvas, Row row, uint32_t now_ms)
{
    if (row.selected) {
        const ThemeColors *colors = &view->theme->colors;
        int edge = max_of(1, view->theme->metrics.scrollbar_inset);
        int cap = max_of(edge, view->theme->metrics.padding / 2);
        canvas_fill_rect(canvas, row.area, colors->selection_background);
        canvas_fill_rect(canvas, (Rect){row.area.x, row.area.y, edge, row.area.height},
                         colors->accent);
        canvas_fill_rect(canvas, (Rect){row.area.x, row.area.y, cap, 1}, colors->accent);
        canvas_fill_rect(canvas, (Rect){row.area.x, row.area.y + row.area.height - 1, cap, 1},
                         colors->accent_alt);
    }
    row = draw_icon(view, canvas, row);
    if (details_below(view)) {
        draw_detail_below(view, canvas, row);
        draw_label(view, canvas, row, now_ms);
    } else {
        draw_label(view, canvas, draw_detail(view, canvas, draw_chevron(view, canvas, row)),
                   now_ms);
    }
}

static void draw_scrollbar(const ListView *view, Canvas *canvas, Rect area)
{
    const Theme *theme = view->theme;
    int hidden = item_count(view) - view->visible_rows;
    int inset = theme->metrics.scrollbar_inset;
    Rect track = {area.x + area.width - inset - theme->metrics.scrollbar_width, area.y + inset,
                  theme->metrics.scrollbar_width, area.height - 2 * inset};
    int thumb_height =
        max_of(MIN_THUMB_HEIGHT, track.height * view->visible_rows / item_count(view));
    int thumb_top = track.y + (track.height - thumb_height) * view->top / hidden;
    canvas_fill_rect(canvas, (Rect){track.x, thumb_top, track.width, thumb_height},
                     theme->colors.scrollbar_thumb);
}

static Row make_row(const ListView *view, Rect rows, int position)
{
    const ThemeMetrics *metrics = &view->theme->metrics;
    int index = view->top + position;
    int height = list_view_row_height(view);
    Rect area = {rows.x, rows.y + position * height, rows.width, height};
    Rect text = {area.x + metrics->padding, area.y, area.width - 2 * metrics->padding,
                 metrics->row_height};
    return (Row){.index = index, .area = area, .text = text, .selected = index == view->selected};
}

static void draw_section(const ListView *view, Canvas *canvas, Rect area, uint32_t now_ms)
{
    if (view->model->section == NULL || item_count(view) <= view->visible_rows ||
        view->section_shown_until_ms == 0 ||
        (int32_t)(now_ms - view->section_shown_until_ms) >= 0) {
        return;
    }
    const Theme *theme = view->theme;
    int side = area.height * 2 / 3 < area.width ? area.height * 2 / 3 : area.width;
    Rect box = {area.x + (area.width - side) / 2, area.y + (area.height - side) / 2, side, side};
    Rect inner = {box.x + 1, box.y + 1, side - 2, side - 2};
    canvas_fill_rect(canvas, box, theme->colors.divider);
    canvas_fill_rect(canvas, inner, theme->colors.header_background);
    TextStyle style = {.font = theme->fonts.title, .color = theme->colors.header_text};
    int scale = max_of(1, side / (font_line_height(style.font) + 4));
    const char *label =
        list_section_label(view->model->section(view->model->context, view->selected));
    text_draw_glyph_scaled(canvas, style, inner, (uint32_t)(unsigned char)label[0], scale);
}

CORE_SEAM void list_view_draw(const ListView *view, Canvas *canvas, Rect area, uint32_t now_ms)
{
    theme_background_draw(view->theme, canvas,
                          view->background == 0 ? THEME_ART_LIST : view->background, area);
    Rect rows = area;
    if (item_count(view) > view->visible_rows) {
        rows.width -= view->theme->metrics.scrollbar_width + view->theme->metrics.scrollbar_inset;
        draw_scrollbar(view, canvas, area);
    }
    for (int position = 0; position < view->visible_rows; position++) {
        Row row = make_row(view, rows, position);
        if (row.index >= item_count(view)) {
            break;
        }
        if (!rect_is_empty(rect_intersection(row.area, canvas->clip))) {
            draw_row(view, canvas, row, now_ms);
        }
    }
    draw_section(view, canvas, area, now_ms);
}
