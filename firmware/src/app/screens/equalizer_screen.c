#include "equalizer_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "ui/header.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

enum {
    SAVE_ROW = SETTINGS_EQUALIZER_BANDS
};

static const char *const BAND_LABELS[SETTINGS_EQUALIZER_BANDS] = {
    "31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};

static void format_gain(char *buffer, int half_db)
{
    StringBuilder builder = string_builder_make(buffer, EQUALIZER_SCREEN_TEXT_CAPACITY);
    int magnitude = half_db < 0 ? -half_db : half_db;
    if (half_db != 0) {
        string_builder_append_char(&builder, half_db < 0 ? '-' : '+');
    }
    string_builder_append_unsigned(&builder, (uint32_t)(magnitude / 2));
    string_builder_append(&builder, magnitude % 2 != 0 ? ".5 dB" : ".0 dB");
}

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
        format_gain(equalizer->text[band], equalizer->config.settings->equalizer_gains[band]);
    }
    format_slot(equalizer);
}

static int band_count(const void *context)
{
    (void)context;
    return EQUALIZER_SCREEN_ROWS;
}

static const char *band_label(const void *context, int index)
{
    (void)context;
    return index == SAVE_ROW ? i18n_text(STR_EQ_SAVE_TO_SLOT) : BAND_LABELS[index];
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
    Settings *settings = equalizer->config.settings;
    int8_t *gain = &settings->equalizer_gains[equalizer->list.selected];
    int next = *gain + step;
    if (next < -SETTINGS_EQUALIZER_GAIN_MAX || next > SETTINGS_EQUALIZER_GAIN_MAX) {
        return;
    }
    *gain = (int8_t)next;
    settings->equalizer = EQUALIZER_CUSTOM;
    refresh_text(equalizer);
    equalizer->config.changed(equalizer->config.context, false);
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

static ScreenAction stop_adjusting(EqualizerScreen *equalizer, ScreenAction action)
{
    equalizer->adjusting = false;
    equalizer->config.changed(equalizer->config.context, true);
    return action;
}

static ScreenAction confirm(EqualizerScreen *equalizer)
{
    if (equalizer->list.selected == SAVE_ROW) {
        equalizer->config.save_slot(equalizer->config.context, equalizer->slot);
        format_slot(equalizer);
    }
    return stop_adjusting(equalizer, screen_action_none());
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
        return stop_adjusting(equalizer, screen_action_none());
    case NAVIGATION_HOME:
        return stop_adjusting(equalizer, screen_action_pop_to_root());
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
    const char *title = equalizer->adjusting ? equalizer->title : equalizer->screen.title;
    header_draw(canvas, theme, title);
    list_view_draw(&equalizer->list, canvas, theme_content_area(theme), equalizer->now_ms);
}

Screen *equalizer_screen_init(EqualizerScreen *equalizer, EqualizerScreenConfig config)
{
    equalizer->config = config;
    equalizer->now_ms = 0;
    equalizer->adjusting = false;
    equalizer->slot = config.slot;
    refresh_text(equalizer);
    equalizer->model = (ListModel){
        .context = equalizer, .count = band_count, .label = band_label, .detail = band_gain};
    list_view_init(&equalizer->list, &equalizer->model, config.theme, 0);
    equalizer->screen = (Screen){
        .title = i18n_text(STR_CUSTOM_EQ),
        .context = equalizer,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &equalizer->screen;
}
