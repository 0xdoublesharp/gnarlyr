#include "equalizer_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "equalizer_plot.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

enum {
    SAVE_ROW = SETTINGS_EQUALIZER_BANDS
};

static void format_slot(EqualizerScreen *equalizer)
{
    StringBuilder builder =
        string_builder_make(equalizer->text[SAVE_ROW], EQUALIZER_SCREEN_TEXT_CAPACITY);
    uint32_t number = (uint32_t)equalizer->slot + 1;
    if (equalizer->config.slot_used(equalizer->config.context, equalizer->slot)) {
        i18n_format_counts(&builder, STR_EQ_SLOT_USED, number, 0);
    } else {
        string_builder_append_unsigned(&builder, number);
    }
}

static void refresh_text(EqualizerScreen *equalizer)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        equalizer_plot_gain(equalizer->text[band], sizeof equalizer->text[band],
                            5 * equalizer->working.equalizer_gains[band]);
    }
    format_slot(equalizer);
    Settings visual = equalizer->working;
    visual.equalizer = EQUALIZER_CUSTOM;
    SettingsEqualizerCurve curve = settings_equalizer_curve(&visual);
    equalizer_response_make(&equalizer->response, &curve);
}

static int band_count(const void *context)
{
    (void)context;
    return EQUALIZER_SCREEN_ROWS;
}

static const char *band_label(const void *context, int index)
{
    (void)context;
    return index == SAVE_ROW ? i18n_text(STR_EQ_SAVE_TO_SLOT) : equalizer_plot_band_name(index);
}

static const char *band_gain(const void *context, int index)
{
    const EqualizerScreen *equalizer = context;
    return equalizer->text[index];
}

static ScreenAction start_adjusting(void *context, int index)
{
    EqualizerScreen *equalizer = context;
    StringBuilder builder = string_builder_make(equalizer->title, sizeof equalizer->title);
    i18n_format_text(&builder, STR_EQ_ADJUSTING, band_label(equalizer, index));
    equalizer->adjusting = true;
    return screen_action_none();
}

static void change_gain(EqualizerScreen *equalizer, int step)
{
    Settings *settings = &equalizer->working;
    int8_t *gain = &settings->equalizer_gains[equalizer->list.selected];
    int next = *gain + step;
    if (next < -SETTINGS_EQUALIZER_GAIN_MAX || next > SETTINGS_EQUALIZER_GAIN_MAX) {
        return;
    }
    *gain = (int8_t)next;
    settings->equalizer = EQUALIZER_CUSTOM;
    refresh_text(equalizer);
    if (equalizer->config.preview != NULL) {
        SettingsEqualizerCurve curve = settings_equalizer_curve(settings);
        equalizer->config.preview(equalizer->config.context, &curve);
        equalizer->previewing = true;
    }
}

static void change_slot(EqualizerScreen *equalizer, int step)
{
    int next = equalizer->slot + step;
    if (next >= 0 && next < EQUALIZER_SLOT_COUNT) {
        equalizer->slot = next;
        format_slot(equalizer);
    }
}

static void change_value(EqualizerScreen *equalizer, int step)
{
    if (equalizer->list.selected == SAVE_ROW) {
        change_slot(equalizer, step);
    } else {
        change_gain(equalizer, step);
    }
}

static void restore(EqualizerScreen *equalizer)
{
    equalizer->working = *equalizer->config.settings;
    refresh_text(equalizer);
    if (equalizer->previewing && equalizer->config.preview != NULL) {
        SettingsEqualizerCurve curve = settings_equalizer_curve(equalizer->config.settings);
        equalizer->config.preview(equalizer->config.context, &curve);
    }
    equalizer->previewing = false;
    equalizer->adjusting = false;
}

static ScreenAction confirm(EqualizerScreen *equalizer)
{
    if (equalizer->list.selected == SAVE_ROW) {
        equalizer->config.save_slot(equalizer->config.context, equalizer->slot);
        format_slot(equalizer);
    } else {
        Settings *settings = equalizer->config.settings;
        settings->equalizer = equalizer->working.equalizer;
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            settings->equalizer_gains[band] = equalizer->working.equalizer_gains[band];
        }
        equalizer->config.changed(equalizer->config.context, true);
    }
    equalizer->previewing = false;
    equalizer->adjusting = false;
    return screen_action_none();
}

static ScreenAction adjust(EqualizerScreen *equalizer, NavigationIntent intent)
{
    switch (intent) {
    case NAVIGATION_NEXT:
    case NAVIGATION_PREVIOUS:
        change_value(equalizer, intent == NAVIGATION_NEXT ? 1 : -1);
        return screen_action_none();
    case NAVIGATION_SELECT:
        return confirm(equalizer);
    case NAVIGATION_BACK:
        restore(equalizer);
        return screen_action_none();
    case NAVIGATION_HOME:
        restore(equalizer);
        return screen_action_pop_to_root();
    case NAVIGATION_OPTIONS:
    case NAVIGATION_NONE:
        break;
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    EqualizerScreen *equalizer = context;
    if (equalizer->adjusting) {
        return adjust(equalizer, navigation_from_key(event).intent);
    }
    ListSelection selection = {.select = start_adjusting, .hold = NULL, .context = equalizer};
    return list_navigation_handle(&equalizer->list, event, now_ms, selection);
}

static void enter(void *context, uint32_t now_ms)
{
    EqualizerScreen *equalizer = context;
    equalizer->now_ms = now_ms;
    equalizer->adjusting = false;
    equalizer->previewing = false;
    equalizer->working = *equalizer->config.settings;
    refresh_text(equalizer);
}

static void update(void *context, uint32_t now_ms)
{
    EqualizerScreen *equalizer = context;
    equalizer->now_ms = now_ms;
}

static void draw(void *context, Canvas *canvas)
{
    const EqualizerScreen *equalizer = context;
    const Theme *theme = equalizer->config.theme;
    StringId hint_id = !equalizer->adjusting                  ? STR_EQ_BROWSE_HINT
                       : equalizer->list.selected == SAVE_ROW ? STR_EQ_SLOT_HINT
                                                              : STR_EQ_ADJUST_HINT;
    const char *hint = i18n_text(hint_id);
    EqualizerPlotLayout layout = equalizer_plot_layout(theme, hint, false);
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    header_draw(canvas, theme, equalizer->screen.title);
    const char *name = band_label(equalizer, equalizer->list.selected);
    const Font *font = text_width(theme->fonts.title, name) <= layout.primary.width
                           ? theme->fonts.title
                           : theme->fonts.small;
    label_draw(canvas, (TextStyle){font, theme->colors.text}, (Label){layout.primary, name});
    Color ink = equalizer->adjusting ? theme->colors.accent_alt : theme->colors.accent;
    label_draw(canvas, (TextStyle){theme->fonts.title, ink},
               (Label){layout.secondary, equalizer->text[equalizer->list.selected]});
    equalizer_plot_draw(canvas, theme, layout.plot,
                        (EqualizerPlotView){.response = &equalizer->response,
                                            .selected_band = equalizer->list.selected,
                                            .adjusting = equalizer->adjusting});
    equalizer_plot_caption(canvas, theme, layout.preamp, &equalizer->response);
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_tertiary},
               (Label){layout.reference, i18n_text(STR_EQ_REFERENCE)});
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){layout.hint, hint});
}

Screen *equalizer_screen_init(EqualizerScreen *equalizer, EqualizerScreenConfig config)
{
    equalizer->config = config;
    equalizer->working = *config.settings;
    equalizer->previewing = false;
    equalizer->now_ms = 0;
    equalizer->adjusting = false;
    equalizer->slot = config.slot;
    refresh_text(equalizer);
    equalizer->model = (ListModel){
        .context = equalizer, .count = band_count, .label = band_label, .detail = band_gain};
    list_view_init(&equalizer->list, &equalizer->model, config.theme, 0);
    equalizer->screen = (Screen){
        .title = i18n_text(STR_CUSTOM_EQ),
        .paints_every_pixel = true,
        .context = equalizer,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &equalizer->screen;
}
