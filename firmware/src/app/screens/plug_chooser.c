#include "plug_chooser.h"

#include "core/deadline.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/status_bar.h"

enum {
    MILLISECONDS_PER_SECOND = 1000,
    WIDE_LINE_ROWS = 2,
    TALL_LINE_ROWS = 3,
    TITLE_ROWS = 2,
    ART_HALF_ROWS = 3,
    COLUMN_SHARE = 4,
    CUE_TEXT_CAPACITY = 24,
    TURNED_WIDE = 13,
    TURNED_TALL = 8,
    STROKE_SHARE = 4,
    BALANCE_STEP = 4
};

static const char CHECK_MARK[] = "\xE2\x9C\x93";
static const FooterHint ALWAYS_HINT[] = {{FOOTER_KEY_HOLD_PLAY, STR_HINT_ALWAYS}};
static const FooterHints ALWAYS_FOOTER = {
    .items = ALWAYS_HINT, .count = 1, .align = FOOTER_ALIGN_END};

typedef struct RowRoom {
    int all;
    int scrolled;
} RowRoom;

PlugMode plug_chooser_mode(const PlugChooser *chooser, int row)
{
    return (PlugMode)chooser->choices.modes[row];
}

static PlugMode selected_mode(const PlugChooser *chooser)
{
    return plug_chooser_mode(chooser, chooser->list.selected);
}

const char *plug_chooser_line(const PlugChooser *chooser)
{
    return i18n_text(plug_mode_line(selected_mode(chooser), chooser->config.state.battery));
}

bool plug_chooser_has_footer(const PlugChooser *chooser)
{
    return chooser->config.state.place != PLUG_PLACE_SETTINGS;
}

static int row_count(const void *context)
{
    const PlugChooser *chooser = context;
    return chooser->choices.count;
}

static const char *row_label(const void *context, int index)
{
    return i18n_text(plug_mode_name(plug_chooser_mode(context, index)));
}

static IconId row_icon(const void *context, int index)
{
    return plug_mode_icon(plug_chooser_mode(context, index));
}

int plug_chooser_shown_rows(const PlugChooser *chooser)
{
    int room = chooser->list_area.height;
    int row = chooser->list.top;
    while (row < chooser->choices.count && chooser->heights[row] <= room) {
        room -= chooser->heights[row];
        row++;
    }
    return row - chooser->list.top;
}

static void keep_focus_shown(PlugChooser *chooser)
{
    ListView *list = &chooser->list;
    if (list->selected < list->top) {
        list->top = list->selected;
    }
    while (list->top < list->selected &&
           list->top + plug_chooser_shown_rows(chooser) <= list->selected) {
        list->top++;
    }
}

static int small_line(const Theme *theme)
{
    return font_line_height(theme->fonts.small);
}

static bool title_in_body(const PlugChooser *chooser)
{
    return chooser->timed && theme_is_portrait(chooser->config.theme);
}

static bool paused_in_header(const PlugChooser *chooser)
{
    if (title_in_body(chooser)) {
        return false;
    }
    const Theme *theme = chooser->config.theme;
    StatusBarState bar = {.battery_known = true,
                          .battery_percent = 100,
                          .usb_power = true,
                          .position = i18n_text(STR_PLUG_PAUSED),
                          .title_width = text_width(theme->fonts.header, chooser->config.title),
                          .logo = true,
                          .hides_charging = true};
    return (status_bar_items(theme, &bar) & STATUS_BAR_POSITION) != 0;
}

static int mark_width(const PlugChooser *chooser)
{
    if (chooser->choices.marked == PLUG_NOT_MARKED) {
        return 0;
    }
    const Theme *theme = chooser->config.theme;
    return text_width(theme->fonts.body, CHECK_MARK) + theme->metrics.padding;
}

static int row_height(const PlugChooser *chooser, int row, int width)
{
    const Theme *theme = chooser->config.theme;
    int pad = theme->metrics.padding;
    int gap = pad - LIST_VIEW_ICON_MIN_GAP;
    int room = width - 2 * pad - LIST_VIEW_ICON_BOX -
               (gap > LIST_VIEW_ICON_MIN_GAP ? gap : LIST_VIEW_ICON_MIN_GAP);
    const Font *font = theme->fonts.title;
    const char *label = row_label(chooser, row);
    if (text_width(font, label) <= room) {
        return theme->metrics.row_height;
    }
    int lines = label_wrap_count(font, label, room);
    lines = lines < 2                             ? 2
            : lines > LIST_VIEW_MAX_WRAPPED_LINES ? LIST_VIEW_MAX_WRAPPED_LINES
                                                  : lines;
    return lines * (font_line_height(font) + LIST_VIEW_DETAIL_LINE_GAP);
}

static int rows_total(PlugChooser *chooser, int width)
{
    chooser->row_width = width;
    list_view_measure(&chooser->list, width);
    int total = 0;
    for (int row = 0; row < chooser->choices.count; row++) {
        int height = row_height(chooser, row, width);
        chooser->heights[row] = (uint8_t)height;
        total += height;
    }
    return total;
}

static int window_height(const PlugChooser *chooser, int first, int room)
{
    int height = 0;
    for (int row = first; row < chooser->choices.count && height + chooser->heights[row] <= room;
         row++) {
        height += chooser->heights[row];
    }
    return height;
}

static int fit_rows(PlugChooser *chooser, int width, RowRoom room)
{
    int total = rows_total(chooser, width - mark_width(chooser));
    if (total <= room.all) {
        return total;
    }
    int fill = 0;
    for (int first = 0; first < chooser->choices.count; first++) {
        int height = window_height(chooser, first, room.scrolled);
        fill = height > fill ? height : fill;
    }
    return fill;
}

static bool scrolls(const PlugChooser *chooser, int region)
{
    int total = 0;
    for (int row = 0; row < chooser->choices.count; row++) {
        total += chooser->heights[row];
    }
    return total > region;
}

static void lay_out_wide(PlugChooser *chooser, Rect body)
{
    const Theme *theme = chooser->config.theme;
    int pad = theme->metrics.padding;
    int column = body.width / COLUMN_SHARE;
    Rect pane = {body.x + column + 1, body.y, body.width - column - 1, body.height};
    int room = pane.height - WIDE_LINE_ROWS * small_line(theme);
    int region = fit_rows(chooser, pane.width, (RowRoom){room, room});
    chooser->list_area = (Rect){pane.x, pane.y, pane.width, region};
    int cue = scrolls(chooser, region) ? small_line(theme) : 0;
    chooser->cue_area = (Rect){body.x, body.y + body.height - pad - cue, column, cue};
}

static int balanced_width(const Font *font, const char *text, int width)
{
    int lines = label_wrap_count(font, text, width);
    int balanced = text_width(font, text) / (lines > 0 ? lines : 1);
    while (balanced < width && label_wrap_count(font, text, balanced) > lines) {
        balanced += BALANCE_STEP;
    }
    return balanced < width ? balanced : width;
}

static Rect title_area(const PlugChooser *chooser, Rect body)
{
    const Theme *theme = chooser->config.theme;
    int pad = theme->metrics.padding;
    if (!title_in_body(chooser)) {
        return (Rect){body.x + pad, body.y + pad / 2, body.width - 2 * pad, 0};
    }
    const Font *font = theme->fonts.header;
    int width = balanced_width(font, chooser->config.title, body.width - 2 * pad);
    int lines = label_wrap_count(font, chooser->config.title, width);
    lines = lines > TITLE_ROWS ? TITLE_ROWS : lines;
    return (Rect){body.x + (body.width - width) / 2, body.y + pad / 2, width,
                  lines * font_line_height(font)};
}

static void lay_out_tall(PlugChooser *chooser, Rect body)
{
    const Theme *theme = chooser->config.theme;
    int pad = theme->metrics.padding;
    int width = body.width - 2 * pad;
    Rect title = title_area(chooser, body);
    int top = title.y + title.height + pad / 2;
    int line = TALL_LINE_ROWS * small_line(theme) + pad;
    int art = ART_HALF_ROWS * theme->metrics.row_height / 2;
    int room = body.y + body.height - top - art - pad / 2 - line;
    int cue = small_line(theme);
    int region = fit_rows(chooser, body.width, (RowRoom){room, room - cue});
    cue = scrolls(chooser, region) ? cue : 0;
    art = body.y + body.height - top - pad / 2 - region - cue - line;
    chooser->list_area = (Rect){body.x, top + art + pad / 2, body.width, region};
    chooser->cue_area = (Rect){body.x + pad, chooser->list_area.y + region, width, cue};
}

static Rect body_area(const PlugChooser *chooser, int *band)
{
    const Theme *theme = chooser->config.theme;
    Rect content = theme_content_area(theme);
    *band = plug_chooser_has_footer(chooser) ? footer_hints_height(theme, ALWAYS_FOOTER) : 0;
    return (Rect){content.x, content.y, content.width, content.height - *band};
}

static void derive_tall(const PlugChooser *chooser, Rect body, PlugChooserLayout *layout)
{
    const Theme *theme = chooser->config.theme;
    int pad = theme->metrics.padding;
    int width = body.width - 2 * pad;
    layout->title = title_area(chooser, body);
    int top = layout->title.y + layout->title.height + pad / 2;
    layout->art = (Rect){body.x + pad, top, width, layout->list.y - pad / 2 - top};
    layout->line = (Rect){body.x + pad, layout->cue.y + layout->cue.height, width,
                          TALL_LINE_ROWS * small_line(theme) + pad};
}

static void derive_wide(const PlugChooser *chooser, Rect body, PlugChooserLayout *layout)
{
    int pad = chooser->config.theme->metrics.padding;
    int column = body.width / COLUMN_SHARE;
    Rect list = layout->list;
    layout->title = title_area(chooser, body);
    layout->column = body.x + column;
    layout->line = (Rect){list.x + pad, list.y + list.height, list.width - 2 * pad,
                          body.y + body.height - list.y - list.height};
    int bottom = body.y + body.height - pad;
    int art_bottom = layout->cue.height > 0 ? layout->cue.y - pad / 2 : bottom;
    layout->art = (Rect){body.x + pad, body.y + pad, column - 2 * pad, art_bottom - body.y - pad};
}

PlugChooserLayout plug_chooser_layout(const PlugChooser *chooser)
{
    int band = 0;
    Rect body = body_area(chooser, &band);
    PlugChooserLayout layout = {.list = chooser->list_area,
                                .cue = chooser->cue_area,
                                .footer = {body.x, body.y + body.height, body.width, band}};
    if (theme_is_portrait(chooser->config.theme)) {
        derive_tall(chooser, body, &layout);
    } else {
        derive_wide(chooser, body, &layout);
    }
    return layout;
}

static void lay_out(PlugChooser *chooser)
{
    const Theme *theme = chooser->config.theme;
    int band = 0;
    Rect body = body_area(chooser, &band);
    list_view_fit_rows(&chooser->list, chooser->choices.count);
    if (theme_is_portrait(theme)) {
        lay_out_tall(chooser, body);
    } else {
        lay_out_wide(chooser, body);
    }
    keep_focus_shown(chooser);
}

static void count_down(PlugChooser *chooser, uint32_t now_ms)
{
    if (!chooser->timed || chooser->paused) {
        return;
    }
    chooser->expired = deadline_reached(now_ms, chooser->until_ms);
    uint32_t left = chooser->expired ? 0
                                     : (chooser->until_ms - now_ms + MILLISECONDS_PER_SECOND - 1) /
                                           MILLISECONDS_PER_SECOND;
    if (left == chooser->seconds) {
        return;
    }
    chooser->seconds = left;
    chooser->redraw = true;
    StringBuilder text = string_builder_make(chooser->countdown, sizeof chooser->countdown);
    i18n_format_counts(&text, STR_PLUG_COUNTDOWN, left, 0);
    chooser->screen.header_status = chooser->countdown;
}

static void pause_countdown(PlugChooser *chooser)
{
    if (!chooser->timed || chooser->paused) {
        return;
    }
    chooser->paused = true;
    chooser->redraw = true;
    chooser->screen.header_status = paused_in_header(chooser) ? i18n_text(STR_PLUG_PAUSED) : NULL;
}

static ScreenAction choose_row(PlugChooser *chooser, int index, bool always)
{
    chooser->config.choose(chooser->config.context, plug_chooser_mode(chooser, index), always);
    return screen_action_pop();
}

static ScreenAction select_row(void *context, int index)
{
    return choose_row(context, index, false);
}

static ScreenAction hold_row(void *context, int index)
{
    PlugChooser *chooser = context;
    if (chooser->config.state.place == PLUG_PLACE_SETTINGS) {
        return screen_action_none();
    }
    return choose_row(chooser, index, true);
}

static void enter(void *context, uint32_t now_ms)
{
    PlugChooser *chooser = context;
    chooser->now_ms = now_ms;
    chooser->until_ms = now_ms + PLUG_PROMPT_MS;
    chooser->seconds = 0;
    list_view_select(&chooser->list, chooser->choices.cursor, now_ms);
    lay_out(chooser);
    count_down(chooser, now_ms);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    PlugChooser *chooser = context;
    if (event.key == KEY_VOLUME_UP || event.key == KEY_VOLUME_DOWN) {
        pause_countdown(chooser);
    }
    ListSelection selection = {.select = select_row, .hold = hold_row, .context = chooser};
    ScreenAction action = list_navigation_handle(&chooser->list, event, now_ms, selection);
    keep_focus_shown(chooser);
    return action;
}

static void update(void *context, uint32_t now_ms)
{
    PlugChooser *chooser = context;
    chooser->now_ms = now_ms;
    count_down(chooser, now_ms);
}

static void draw_drain(const PlugChooser *chooser, Canvas *canvas)
{
    if (!chooser->timed || chooser->paused) {
        return;
    }
    const Theme *theme = chooser->config.theme;
    Rect rule = {0, theme->metrics.header_height - 1, theme->metrics.screen_width, 1};
    canvas_fill_rect(canvas, rule, theme->colors.divider);
    rule.width = rule.width * (int)chooser->seconds * MILLISECONDS_PER_SECOND / PLUG_PROMPT_MS;
    canvas_fill_rect(canvas, rule, theme->colors.accent);
}

static void draw_mark(const PlugChooser *chooser, Canvas *canvas, Rect row, int index)
{
    const Theme *theme = chooser->config.theme;
    Rect cell = {row.x + row.width, row.y, mark_width(chooser), row.height};
    if (cell.width == 0) {
        return;
    }
    if (index == chooser->list.selected) {
        canvas_fill_rect(canvas, cell, theme_selection_fill(theme));
    }
    if (index == chooser->choices.marked) {
        label_draw(canvas, (TextStyle){theme->fonts.body, theme->colors.accent, false},
                   (Label){cell, CHECK_MARK});
    }
}

static void draw_rows(const PlugChooser *chooser, Canvas *canvas)
{
    Rect area = chooser->list_area;
    theme_background_draw(chooser->config.theme, canvas, THEME_ART_LIST, area);
    int shown = plug_chooser_shown_rows(chooser);
    Rect row = {area.x, area.y, chooser->row_width, 0};
    for (int index = chooser->list.top; index < chooser->list.top + shown; index++) {
        row.height = chooser->heights[index];
        list_view_draw_row(&chooser->list, canvas, index, row, chooser->now_ms);
        draw_mark(chooser, canvas, row, index);
        row.y += row.height;
    }
    if (shown < chooser->choices.count) {
        list_view_draw_scrollbar(&chooser->list, canvas, area, chooser->choices.count - shown);
    }
}

static void draw_turned(Canvas *canvas, Rect box, bool down, Color ink)
{
    int stroke = box.width / STROKE_SHARE;
    for (int row = 0; row < box.height; row++) {
        int step = (down ? row : box.height - 1 - row) * (box.width - stroke) / 2 / box.height;
        canvas_fill_rect(canvas, (Rect){box.x + step, box.y + row, stroke, 1}, ink);
        canvas_fill_rect(canvas, (Rect){box.x + box.width - step - stroke, box.y + row, stroke, 1},
                         ink);
    }
}

typedef struct CuePlan {
    bool up;
    bool down;
    int gap;
    int width;
    Rect turned;
} CuePlan;

static CuePlan cue_plan(const PlugChooser *chooser, const char *text, int shown)
{
    const Theme *theme = chooser->config.theme;
    Rect cue = chooser->cue_area;
    int tall = list_view_chevron_width(theme);
    CuePlan plan = {.up = chooser->list.top > 0,
                    .down = chooser->list.top + shown < chooser->choices.count,
                    .gap = theme->metrics.padding / 2};
    plan.turned =
        (Rect){0, cue.y + (cue.height - tall) / 2, tall * TURNED_WIDE / TURNED_TALL, tall};
    int chevron = plan.turned.width + plan.gap;
    plan.width =
        text_width(theme->fonts.small, text) + (plan.up ? chevron : 0) + (plan.down ? chevron : 0);
    return plan;
}

static void draw_cue(const PlugChooser *chooser, Canvas *canvas)
{
    const Theme *theme = chooser->config.theme;
    Rect cue = chooser->cue_area;
    int shown = plug_chooser_shown_rows(chooser);
    if (cue.height == 0 || shown >= chooser->choices.count) {
        return;
    }
    char text[CUE_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_PLACE, (uint32_t)chooser->list.selected + 1,
                       chooser->choices.count);
    CuePlan plan = cue_plan(chooser, text, shown);
    bool centred = !theme_is_portrait(theme);
    plan.turned.x = cue.x + (centred ? (cue.width - plan.width) / 2 : cue.width - plan.width);
    Color ink = theme->colors.text_secondary;
    if (plan.up) {
        draw_turned(canvas, plan.turned, false, ink);
        plan.turned.x += plan.turned.width + plan.gap;
    }
    Rect words = {plan.turned.x, cue.y, text_width(theme->fonts.small, text), cue.height};
    label_draw(canvas, (TextStyle){theme->fonts.small, ink, false}, (Label){words, text});
    plan.turned.x = words.x + words.width + plan.gap;
    if (plan.down) {
        draw_turned(canvas, plan.turned, true, ink);
    }
}

static void draw_line(const PlugChooser *chooser, Canvas *canvas)
{
    const Theme *theme = chooser->config.theme;
    Rect line = plug_chooser_layout(chooser).line;
    int pad = theme->metrics.padding;
    canvas_fill_rect(canvas, (Rect){line.x - pad, line.y, line.width + 2 * pad, 1},
                     theme->colors.divider);
    const char *text = plug_chooser_line(chooser);
    int height = small_line(theme);
    int room = theme_is_portrait(theme) ? TALL_LINE_ROWS : WIDE_LINE_ROWS;
    int lines = label_wrap_count(theme->fonts.small, text, line.width);
    lines = lines > room ? room : lines;
    Rect words = {line.x, line.y + (line.height - lines * height) / 2, line.width, lines * height};
    label_draw_paragraph(canvas,
                         (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
                         (Label){words, text});
}

static void draw_body(const PlugChooser *chooser, Canvas *canvas)
{
    const Theme *theme = chooser->config.theme;
    PlugChooserLayout shown = plug_chooser_layout(chooser);
    const PlugChooserLayout *layout = &shown;
    if (layout->title.height > 0) {
        label_draw_wrapped(canvas, (TextStyle){theme->fonts.header, theme->colors.text, false},
                           (Label){layout->title, chooser->config.title});
    }
    if (layout->column > 0) {
        Rect content = theme_content_area(theme);
        canvas_fill_rect(canvas, (Rect){layout->column, content.y, 1, layout->footer.y - content.y},
                         theme->colors.divider);
    }
    Rect art = layout->art;
    if (chooser->paused && chooser->screen.header_status == NULL) {
        const char *paused = i18n_text(STR_PLUG_PAUSED);
        int height = label_wrap_count(theme->fonts.small, paused, art.width) * small_line(theme);
        art.height -= height + theme->metrics.padding / 2;
        label_draw_paragraph(
            canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
            (Label){{art.x, art.y + art.height + theme->metrics.padding / 2, art.width, height},
                    paused});
    }
    plug_art_draw(canvas, theme, art, selected_mode(chooser));
}

static void draw(void *context, Canvas *canvas)
{
    const PlugChooser *chooser = context;
    const Theme *theme = chooser->config.theme;
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    header_draw(canvas, theme, chooser->screen.untitled_header ? NULL : chooser->config.title);
    draw_drain(chooser, canvas);
    draw_body(chooser, canvas);
    draw_rows(chooser, canvas);
    draw_cue(chooser, canvas);
    draw_line(chooser, canvas);
    if (plug_chooser_has_footer(chooser)) {
        footer_hints_draw(canvas, theme, plug_chooser_layout(chooser).footer, ALWAYS_FOOTER);
    }
}

static void reset(PlugChooser *chooser, PlugChooserConfig config)
{
    chooser->config = config;
    chooser->choices = plug_modes_choices(config.state);
    chooser->now_ms = 0;
    chooser->timed = config.state.place == PLUG_PLACE_PROMPT;
    chooser->paused = false;
    chooser->expired = false;
    chooser->redraw = false;
    chooser->countdown[0] = '\0';
    chooser->model = (ListModel){.context = chooser,
                                 .count = row_count,
                                 .label = row_label,
                                 .icon = row_icon,
                                 .wraps = true};
    list_view_init(&chooser->list, &chooser->model, config.theme, 0);
}

Screen *plug_chooser_init(PlugChooser *chooser, PlugChooserConfig config)
{
    reset(chooser, config);
    chooser->screen = (Screen){
        .title = config.title,
        .context = chooser,
        .paints_every_pixel = true,
        .keys = config.state.place != PLUG_PLACE_SETTINGS ? SCREEN_KEYS_ALL : SCREEN_KEYS_SHARED,
        .stays_on_wake = true,
        .untitled_header = title_in_body(chooser),
        .hides_charging = chooser->timed,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw};
    lay_out(chooser);
    return &chooser->screen;
}
