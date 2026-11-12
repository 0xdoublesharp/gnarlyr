#include "parametric_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "equalizer_plot.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"

enum {
    ENABLED,
    TYPE,
    FREQUENCY,
    GAIN,
    QUALITY,
    REMOVE,
    FIELD_COUNT
};

static EqualizerResponse response_buffer;

static char labels[PARAMETRIC_FILTERS + 3][48];
static char details[PARAMETRIC_FILTERS + 3][48];

static const StringId TYPES[] = {STR_PEQ_PEAK, STR_PEQ_LOW_SHELF, STR_PEQ_HIGH_SHELF};
static const StringId FIELDS[] = {STR_PEQ_ENABLED, STR_PEQ_TYPE, STR_PEQ_FREQUENCY,
                                  STR_GAIN,        STR_PEQ_Q,    STR_PEQ_REMOVE};

static int rows(const void *context)
{
    const ParametricScreen *editor = context;
    return editor->shaping_mode ? SOUND_SHAPING_COUNT + 3
           : editor->filter < 0 ? editor->working.count + 3
                                : FIELD_COUNT;
}

static const char *label(const void *context, int index)
{
    const ParametricScreen *editor = context;
    return editor->labels[index];
}

static const char *detail(const void *context, int index)
{
    const ParametricScreen *editor = context;
    return editor->details[index];
}

static void format_q(StringBuilder *builder, uint16_t q)
{
    string_builder_append_unsigned(builder, q / 100);
    string_builder_append_char(builder, '.');
    if (q % 100 < 10) {
        string_builder_append_char(builder, '0');
    }
    string_builder_append_unsigned(builder, q % 100);
}

static void format_filter(ParametricScreen *editor, int index)
{
    const ParametricFilter *filter = &editor->working.filters[index];
    StringBuilder builder =
        string_builder_make(editor->labels[index], sizeof editor->labels[index]);
    i18n_format_counts(&builder, STR_PEQ_FILTER, (uint32_t)index + 1, 0);
    builder = string_builder_make(editor->details[index], sizeof editor->details[index]);
    string_builder_append(&builder, i18n_text(filter->enabled ? TYPES[filter->type] : STR_OFF));
    string_builder_append_char(&builder, ' ');
    i18n_format_counts(&builder, STR_PEQ_HZ, filter->hz, 0);
}

static void texts(ParametricScreen *editor)
{
    for (int row = 0; row < PARAMETRIC_FILTERS + 3; row++) {
        editor->labels[row][0] = '\0';
        editor->details[row][0] = '\0';
    }
    if (editor->filter < 0) {
        for (int index = 0; index < editor->working.count; index++) {
            format_filter(editor, index);
        }
        StringId actions[] = {STR_PEQ_ADD, STR_PEQ_PREAMP, STR_EQ_SAVE_TO_SLOT};
        for (int action = 0; action < 3; action++) {
            StringBuilder builder =
                string_builder_make(editor->labels[editor->working.count + action], 48);
            string_builder_append(&builder, i18n_text(actions[action]));
        }
        equalizer_plot_gain(editor->details[editor->working.count + 1], 48, editor->working.preamp);
        StringBuilder builder = string_builder_make(editor->details[editor->working.count + 2], 48);
        string_builder_append_unsigned(&builder, (uint32_t)editor->slot + 1);
    } else {
        const ParametricFilter *filter = &editor->working.filters[editor->filter];
        for (int field = 0; field < FIELD_COUNT; field++) {
            StringBuilder builder = string_builder_make(editor->labels[field], 48);
            string_builder_append(&builder, i18n_text(FIELDS[field]));
        }
        StringBuilder builder = string_builder_make(editor->details[ENABLED], 48);
        string_builder_append(&builder, i18n_text(filter->enabled ? STR_ON : STR_OFF));
        builder = string_builder_make(editor->details[TYPE], 48);
        string_builder_append(&builder, i18n_text(TYPES[filter->type]));
        builder = string_builder_make(editor->details[FREQUENCY], 48);
        i18n_format_counts(&builder, STR_PEQ_HZ, filter->hz, 0);
        equalizer_plot_gain(editor->details[GAIN], 48, filter->gain);
        builder = string_builder_make(editor->details[QUALITY], 48);
        format_q(&builder, filter->q);
    }
}

static void response(ParametricScreen *editor)
{
    SettingsEqualizerCurve curve = {.is_parametric = true, .parametric = editor->working};
    equalizer_response_make(editor->response, &curve);
    texts(editor);
}

static void preview(ParametricScreen *editor)
{
    response(editor);
    if (editor->config.preview != NULL) {
        SettingsEqualizerCurve curve = {.is_parametric = true, .parametric = editor->working};
        editor->config.preview(editor->config.context, &curve);
    }
    editor->previewing = true;
}

static void restore(ParametricScreen *editor)
{
    editor->working = editor->kept;
    editor->adjusting = false;
    response(editor);
    if (editor->previewing && editor->config.preview != NULL) {
        SettingsEqualizerCurve curve = settings_equalizer_curve(editor->config.settings);
        editor->config.preview(editor->config.context, &curve);
    }
    editor->previewing = false;
}

static bool commit(ParametricScreen *editor, int slot)
{
    if (!editor->config.commit(editor->config.context, &editor->working, slot)) {
        restore(editor);
        return false;
    }
    editor->kept = editor->working;
    editor->adjusting = false;
    editor->previewing = false;
    response(editor);
    return true;
}

static int bounded(int value, int minimum, int maximum)
{
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void change(ParametricScreen *editor, int step)
{
    int row = editor->list.selected;
    if (editor->filter < 0) {
        if (row == editor->working.count + 2) {
            editor->slot = bounded(editor->slot + step, 0, EQUALIZER_SLOT_COUNT - 1);
            texts(editor);
            return;
        }
        editor->working.preamp = (int16_t)bounded(editor->working.preamp + step,
                                                  PARAMETRIC_MIN_PREAMP, PARAMETRIC_MAX_PREAMP);
    } else {
        ParametricFilter *filter = &editor->working.filters[editor->filter];
        if (row == ENABLED) {
            filter->enabled = step > 0;
        } else if (row == TYPE) {
            filter->type = (uint8_t)bounded(filter->type + step, 0, PARAMETRIC_HIGH_SHELF);
        } else if (row == FREQUENCY) {
            int delta = (filter->hz * 293 + 5000) / 10000;
            filter->hz = (uint16_t)bounded(filter->hz + step * (delta > 0 ? delta : 1), 20, 20000);
        } else if (row == GAIN) {
            filter->gain = (int16_t)bounded(filter->gain + step, -120, 120);
        } else if (row == QUALITY) {
            filter->q = (uint16_t)bounded(filter->q + step, 30, 1000);
        }
    }
    preview(editor);
}

static void select(ParametricScreen *editor)
{
    int row = editor->list.selected;
    if (editor->filter < 0 && row < editor->working.count) {
        editor->filter = row;
        list_view_select(&editor->list, 0, editor->now_ms);
    } else if (editor->filter < 0 && row == editor->working.count) {
        if (editor->working.count == PARAMETRIC_FILTERS) {
            return;
        }
        int added = editor->working.count++;
        editor->working.filters[added] = (ParametricFilter){1000, 71, 0, PARAMETRIC_PEAK, true};
        if (commit(editor, -1)) {
            editor->filter = added;
            list_view_select(&editor->list, 0, editor->now_ms);
        }
    } else if (editor->filter >= 0 && row == REMOVE) {
        int removed = editor->filter;
        for (int index = removed + 1; index < editor->working.count; index++) {
            editor->working.filters[index - 1] = editor->working.filters[index];
        }
        editor->working.count--;
        if (commit(editor, -1)) {
            editor->filter = -1;
            list_view_select(&editor->list, removed, editor->now_ms);
        }
    } else {
        editor->adjusting = true;
    }
    texts(editor);
}

static ScreenAction key(void *context, KeyEvent event, uint32_t now_ms)
{
    ParametricScreen *editor = context;
    editor->now_ms = now_ms;
    NavigationIntent intent = navigation_from_key(event).intent;
    if (editor->shaping_mode) {
        return sound_shaping_screen_key(editor, intent);
    }
    if (intent == NAVIGATION_NEXT || intent == NAVIGATION_PREVIOUS) {
        int step = intent == NAVIGATION_NEXT ? 1 : -1;
        if (editor->adjusting) {
            change(editor, step);
        } else {
            list_view_move(&editor->list, step, false, now_ms);
        }
    } else if (intent == NAVIGATION_SELECT) {
        if (editor->adjusting) {
            bool save = editor->filter < 0 && editor->list.selected == editor->working.count + 2;
            commit(editor, save ? editor->slot : -1);
        } else {
            select(editor);
        }
    } else if (intent == NAVIGATION_BACK || intent == NAVIGATION_HOME) {
        if (editor->adjusting) {
            restore(editor);
        } else if (editor->filter >= 0 && intent != NAVIGATION_HOME) {
            int previous = editor->filter;
            editor->filter = -1;
            texts(editor);
            list_view_select(&editor->list, previous, now_ms);
        } else {
            restore(editor);
            return intent == NAVIGATION_HOME ? screen_action_pop_to_root() : screen_action_pop();
        }
    }
    return screen_action_none();
}

static void draw_rows(ParametricScreen *editor, Canvas *canvas, Rect area)
{
    const Theme *theme = editor->config.theme;
    for (int row = 0; row < editor->list.visible_rows && row + editor->list.top < rows(editor);
         row++) {
        int index = row + editor->list.top;
        Rect bounds = {area.x, area.y + row * theme->metrics.row_height, area.width,
                       theme->metrics.row_height};
        if (index == editor->list.selected) {
            canvas_fill_rect(canvas, bounds, theme->colors.header_background);
            canvas_fill_rect(
                canvas, (Rect){bounds.x, bounds.y, theme->metrics.scrollbar_inset, bounds.height},
                theme->colors.accent);
        }
        bounds.x += theme->metrics.padding / 2;
        bounds.width -= theme->metrics.padding;
        const Font *font = text_width(theme->fonts.body, editor->labels[index]) <= bounds.width
                               ? theme->fonts.body
                               : theme->fonts.small;
        label_draw(canvas, (TextStyle){font, theme->colors.text},
                   (Label){bounds, editor->labels[index]});
    }
}

static void summary(const ParametricScreen *editor, StringBuilder *builder)
{
    int row = editor->list.selected;
    if (!editor->shaping_mode && editor->filter < 0 && row < editor->working.count) {
        const ParametricFilter *filter = &editor->working.filters[row];
        string_builder_append(builder, editor->details[row]);
        string_builder_append_char(builder, ' ');
        char gain[24];
        equalizer_plot_gain(gain, sizeof gain, filter->gain);
        string_builder_append(builder, gain);
        string_builder_append_char(builder, ' ');
        string_builder_append(builder, i18n_text(STR_PEQ_Q));
        string_builder_append_char(builder, ' ');
        format_q(builder, filter->q);
    } else {
        string_builder_append(builder, editor->labels[row]);
        string_builder_append_char(builder, ' ');
        string_builder_append(builder, editor->details[row]);
    }
}

static void draw(void *context, Canvas *canvas)
{
    ParametricScreen *editor = context;
    const Theme *theme = editor->config.theme;
    StringId hint_id = editor->shaping_mode
                           ? editor->adjusting ? STR_SHAPE_EDIT_HINT : STR_SHAPE_HINT
                       : editor->adjusting ? STR_PEQ_EDIT_HINT
                                           : STR_PEQ_BROWSE_HINT;
    const char *hint = i18n_text(hint_id);
    EqualizerPlotLayout layout = equalizer_plot_layout(theme, hint, true);
    int small = font_line_height(theme->fonts.small);
    char text[128];
    StringBuilder builder = string_builder_make(text, sizeof text);
    summary(editor, &builder);
    bool portrait = theme_is_portrait(theme);
    bool filter_summary = !editor->shaping_mode && editor->filter < 0 &&
                          editor->list.selected < editor->working.count;
    const char *detail_text =
        portrait && !filter_summary ? editor->details[editor->list.selected] : text;
    layout.primary.height = portrait ? layout.primary.height : small;
    int lines =
        portrait ? label_wrap_count(theme->fonts.small, detail_text, layout.primary.width) : 1;
    layout.secondary = (Rect){layout.primary.x, layout.primary.y + layout.primary.height,
                              layout.primary.width, small * lines};
    int chart_top =
        (portrait ? layout.secondary.y + layout.secondary.height : layout.primary.y + small) +
        theme->metrics.padding / 2;
    layout.plot.height -= chart_top - layout.plot.y;
    layout.plot.y = chart_top;
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    header_draw(canvas, theme, editor->screen.title);
    list_view_fit_rows(&editor->list, layout.list.height / theme->metrics.row_height);
    draw_rows(editor, canvas, layout.list);
    if (portrait) {
        int row = editor->list.selected;
        label_draw(canvas, (TextStyle){theme->fonts.title, theme->colors.text},
                   (Label){layout.primary, editor->labels[row]});
        label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.accent},
                           (Label){layout.secondary, detail_text});
    } else {
        label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.accent},
                   (Label){layout.primary, text});
    }
    if (editor->shaping_mode) {
        layout.plot = sound_shaping_screen_slider(editor, canvas, layout.plot);
    }
    if (editor->shaping_mode && editor->notice != STRING_COUNT) {
        label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.danger},
                           (Label){layout.plot, i18n_text(editor->notice)});
    } else {
        equalizer_plot_draw(canvas, theme, layout.plot,
                            (EqualizerPlotView){.response = editor->response,
                                                .selected_band = -1,
                                                .adjusting = editor->adjusting});
    }
    equalizer_plot_caption(canvas, theme, layout.preamp, editor->response);
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_tertiary},
               (Label){layout.reference, i18n_text(STR_EQ_REFERENCE)});
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){layout.hint, hint});
}

static void enter(void *context, uint32_t now_ms)
{
    ParametricScreen *editor = context;
    editor->working = editor->config.settings->parametric;
    editor->kept = editor->working;
    editor->filter = -1;
    editor->now_ms = now_ms;
    editor->adjusting = false;
    editor->previewing = false;
    list_view_select(&editor->list, 0, now_ms);
    editor->shaping_mode = editor->shaping_requested;
    editor->shaping_requested = false;
    editor->screen.title = i18n_text(editor->shaping_mode ? STR_SOUND_SHAPING : STR_PEQ);
    if (editor->shaping_mode) {
        sound_shaping_screen_enter(editor);
    } else {
        response(editor);
    }
}

Screen *parametric_screen_init(ParametricScreen *editor, ParametricScreenConfig config)
{
    *editor = (ParametricScreen){.config = config,
                                 .response = &response_buffer,
                                 .working = config.settings->parametric,
                                 .kept = config.settings->parametric,
                                 .filter = -1,
                                 .slot = config.slot,
                                 .labels = labels,
                                 .details = details};
    editor->model = (ListModel){.context = editor, .count = rows, .label = label, .detail = detail};
    list_view_init(&editor->list, &editor->model, config.theme, 0);
    editor->screen = (Screen){.title = i18n_text(STR_PEQ),
                              .context = editor,
                              .enter = enter,
                              .handle_key = key,
                              .draw = draw,
                              .paints_every_pixel = true};
    response(editor);
    return &editor->screen;
}
