#include "quick_settings_screen.h"

#include <stddef.h>

#include "ab_repeat.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "settings_actions.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"

typedef enum QuickRowKind {
    QUICK_CYCLE,
    QUICK_SUBMENU,
    QUICK_PUSH,
    QUICK_ACTION
} QuickRowKind;

typedef struct QuickRow {
    StringId label;
    StringId title;
    uint8_t kind;
    uint8_t count;
    uint8_t target;
    const StringId *values;
    int (*count_of)(const void *settings);
    int (*current)(const void *settings);
    const char *const *(*labels)(const SettingsContext *settings);
    void (*choose)(void *settings, int index);
} QuickRow;

static const char *headphone_names[HEADPHONE_PROFILE_LIMIT + 1];
static char slot_names[HEADPHONE_PROFILE_LIMIT][HEADPHONE_PROFILE_NAME_BYTES];

static const StringId SHUFFLE_VALUES[] = {STR_OFF, STR_SHUFFLE_SONGS, STR_SHUFFLE_ALBUMS};
static const StringId REPEAT_VALUES[] = {STR_OFF, STR_REPEAT_MODE_ONE, STR_REPEAT_MODE_ALL};
static const StringId TRANSITION_VALUES[] = {STR_OFF, STR_TRANSITION_FADE};
static const StringId REPLAY_VALUES[] = {STR_OFF, STR_REPLAY_GAIN_TRACK, STR_REPLAY_GAIN_ALBUM,
                                         STR_REPLAY_GAIN_AUTO};
static const StringId AB_VALUES[] = {STR_OFF, STR_AB_ROW_A, STR_AB_ROW_LOOP};
static const StringId STYLE_VALUES[] = {STR_NOW_PLAYING_COVER, STR_NOW_PLAYING_CASSETTE,
                                        STR_NOW_PLAYING_VU, STR_NOW_PLAYING_SPECTRUM};

static int equalizer_count(const void *settings)
{
    return EQUALIZER_PRESET_COUNT + ((const SettingsContext *)settings)->presets->slot_count;
}

static const char *const *equalizer_labels(const SettingsContext *settings)
{
    return settings->presets->options;
}

static int speed_count(const void *settings)
{
    return settings_action_speed_available(settings) ? SETTINGS_SPEED_COUNT : 0;
}

static int headphone_current(const void *settings)
{
    const HeadphoneProfiles *profiles = &((const SettingsContext *)settings)->headphones->profiles;
    return profiles->current == HEADPHONE_PROFILE_NONE ? 0 : profiles->current + 1;
}

static const char *const *headphone_labels(const SettingsContext *settings)
{
    const HeadphoneProfiles *profiles = &settings->headphones->profiles;
    headphone_names[0] = i18n_text(STR_OFF);
    for (int slot = 0; slot < HEADPHONE_PROFILE_LIMIT; slot++) {
        headphone_names[slot + 1] =
            slot < profiles->count ? profiles->slots[slot].name : slot_names[slot];
        if (slot >= profiles->count) {
            StringBuilder text =
                string_builder_make(slot_names[slot], HEADPHONE_PROFILE_NAME_BYTES);
            i18n_format_counts(&text, STR_HEADPHONE_SLOT, (uint32_t)slot + 1, 0);
        }
    }
    return headphone_names;
}

static void headphone_choose(void *context, int index)
{
    SettingsContext *settings = context;
    HeadphoneProfiles profiles = settings->headphones->profiles;
    headphone_store_fill_slots(&profiles, settings->store->settings.crossfeed);
    profiles.current = index == 0 ? HEADPHONE_PROFILE_NONE : (uint8_t)(index - 1);
    if (!headphone_store_commit(settings->headphones, &profiles, settings->player,
                                settings->store)) {
        toast_show(settings->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
}

static int ab_current(const void *settings)
{
    return ((const SettingsContext *)settings)->player->ab_repeat % 3u;
}

static void ab_step(void *context, int index)
{
    (void)index;
    SettingsContext *settings = context;
    ab_repeat_step(settings->player, settings->power, settings->toast, hal_clock_milliseconds());
}

static const QuickRow ROWS[] = {
    {STR_SHUFFLE, STR_SHUFFLE, QUICK_CYCLE, SHUFFLE_MODE_COUNT, 0, SHUFFLE_VALUES, NULL,
     settings_action_current_shuffle, NULL, settings_action_choose_shuffle},
    {STR_REPEAT, STR_REPEAT, QUICK_CYCLE, REPEAT_MODE_COUNT, 0, REPEAT_VALUES, NULL,
     settings_action_current_repeat, NULL, settings_action_choose_repeat},
    {STR_EQUALIZER, STR_EQUALIZER, QUICK_SUBMENU, 0, 0, NULL, equalizer_count,
     settings_action_current_equalizer, equalizer_labels, settings_action_choose_equalizer},
    {STR_HEADPHONES, STR_HEADPHONES, QUICK_SUBMENU, HEADPHONE_PROFILE_LIMIT + 1, 0, NULL, NULL,
     headphone_current, headphone_labels, headphone_choose},
    {STR_SPEED, STR_PLAYBACK_SPEED, QUICK_SUBMENU, 0, 0, SETTINGS_SPEED_LABELS, speed_count,
     settings_action_current_speed, NULL, settings_action_choose_speed},
    {STR_SLEEP_TIMER, STR_SLEEP_TIMER, QUICK_SUBMENU, SLEEP_TIMER_OPTION_COUNT, 0,
     SETTINGS_ACTION_SLEEP_LABELS, NULL, settings_action_current_sleep, NULL,
     settings_action_choose_sleep},
    {STR_TRANSITIONS, STR_TRANSITIONS, QUICK_CYCLE, 2, 0, TRANSITION_VALUES, NULL,
     settings_action_current_transition, NULL, settings_action_choose_transition},
    {STR_REPLAY_GAIN, STR_REPLAY_GAIN, QUICK_SUBMENU, REPLAY_GAIN_MODE_COUNT, 0, REPLAY_VALUES,
     NULL, settings_action_current_replay_gain, NULL, settings_action_choose_replay_gain},
    {STR_AB_LOOP, STR_AB_LOOP, QUICK_ACTION, 3, 0, AB_VALUES, NULL, ab_current, NULL, ab_step},
    {STR_NOW_PLAYING, STR_NOW_PLAYING_STYLE, QUICK_SUBMENU, 4, 0, STYLE_VALUES, NULL,
     settings_action_current_now_playing, NULL, settings_action_choose_now_playing},
    {STR_LYRICS, STR_LYRICS, QUICK_PUSH, 1, offsetof(QuickSettingsConfig, lyrics), NULL, NULL, NULL,
     NULL, NULL},
    {STR_UP_NEXT, STR_UP_NEXT, QUICK_PUSH, 1, offsetof(QuickSettingsConfig, up_next), NULL, NULL,
     NULL, NULL, NULL},
};

enum {
    ROW_COUNT = sizeof ROWS / sizeof ROWS[0]
};

static int value_count(const QuickSettingsScreen *panel, const QuickRow *row)
{
    return row->count_of != NULL ? row->count_of(&panel->config.settings) : row->count;
}

static const QuickRow *row_at(const QuickSettingsScreen *panel, int index)
{
    const QuickRow *row = ROWS;
    while (value_count(panel, row) == 0 || index-- > 0) {
        row++;
    }
    return row;
}

static int count(const void *context)
{
    int shown = 0;
    for (int index = 0; index < ROW_COUNT; index++) {
        shown += value_count(context, &ROWS[index]) > 0;
    }
    return shown;
}

static const char *label(const void *context, int index)
{
    return i18n_text(row_at(context, index)->label);
}

static const char *detail(const void *context, int index)
{
    const QuickSettingsScreen *panel = context;
    const QuickRow *row = row_at(panel, index);
    if (row->current == NULL) {
        return NULL;
    }
    int current = row->current(&panel->config.settings);
    return row->values != NULL ? i18n_text(row->values[current])
                               : row->labels(&panel->config.settings)[current];
}

static bool opens(const void *context, int index)
{
    uint8_t kind = row_at(context, index)->kind;
    return kind == QUICK_SUBMENU || kind == QUICK_PUSH;
}

static Screen *open_values(QuickSettingsScreen *panel, const QuickRow *row)
{
    panel->values_row = (uint8_t)(row - ROWS);
    ChoiceConfig config = {.title = i18n_text(row->title),
                           .options =
                               row->labels != NULL ? row->labels(&panel->config.settings) : NULL,
                           .option_ids = row->values,
                           .option_count = value_count(panel, row),
                           .current = row->current,
                           .choose = row->choose,
                           .context = &panel->config.settings,
                           .theme = panel->config.theme};
    return choice_screen_init(&panel->values, config);
}

void quick_settings_screen_refit(QuickSettingsScreen *panel)
{
    ChoiceScreen *values = &panel->values;
    if (values->screen.context == NULL) {
        return;
    }
    const QuickRow *row = &ROWS[panel->values_row];
    int shown = value_count(panel, row);
    values->config.option_count = shown;
    values->config.options = row->labels != NULL ? row->labels(&panel->config.settings) : NULL;
    if (values->list.selected >= shown && shown > 0) {
        list_view_select(&values->list, shown - 1, values->now_ms);
    }
}

static ScreenAction select_row(void *context, int index)
{
    QuickSettingsScreen *panel = context;
    const QuickRow *row = row_at(panel, index);
    SettingsContext *settings = &panel->config.settings;
    switch (row->kind) {
    case QUICK_PUSH:
        return screen_action_push(*(Screen **)((char *)&panel->config + row->target));
    case QUICK_SUBMENU:
        return screen_action_push(open_values(panel, row));
    case QUICK_CYCLE:
        row->choose(settings, (row->current(settings) + 1) % row->count);
        break;
    default:
        row->choose(settings, 0);
    }
    list_view_restart_marquee(&panel->list, panel->now_ms);
    return screen_action_none();
}

static Rect rows_area(const Theme *theme)
{
    Rect area = theme_content_area(theme);
    area.height -= font_line_height(theme->fonts.small) + theme->metrics.padding;
    return area;
}

static void refresh_selection(QuickSettingsScreen *panel, uint32_t now_ms)
{
    panel->now_ms = now_ms;
    if (panel->list.selected >= count(panel)) {
        list_view_select(&panel->list, count(panel) - 1, now_ms);
    }
}

static void enter(void *context, uint32_t now_ms)
{
    QuickSettingsScreen *panel = context;
    refresh_selection(panel, now_ms);
    equalizer_presets_scan(panel->config.settings.presets);
    list_view_fit_rows(&panel->list,
                       list_view_rows_in(&panel->list, rows_area(panel->config.theme).height));
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    QuickSettingsScreen *panel = context;
    refresh_selection(panel, now_ms);
    return list_navigation_handle(&panel->list, event, now_ms,
                                  (ListSelection){.select = select_row, .context = panel});
}

static void update(void *context, uint32_t now_ms)
{
    refresh_selection(context, now_ms);
}

static void draw(void *context, Canvas *canvas)
{
    QuickSettingsScreen *panel = context;
    const Theme *theme = panel->config.theme;
    canvas_fill_rect(canvas, theme_content_area(theme), theme->colors.background);
    header_draw(canvas, theme, panel->screen.title);
    Rect area = rows_area(theme);
    list_view_draw(&panel->list, canvas, area, panel->now_ms);
    Rect hint = theme_content_area(theme);
    hint.y += area.height;
    hint.height -= area.height;
    hint.x += theme->metrics.padding;
    hint.width -= 2 * theme->metrics.padding;
    canvas_fill_rect(canvas, hint, theme->colors.background);
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
               (Label){hint, i18n_text(STR_QUICK_SETTINGS_HINT)});
}

Screen *quick_settings_screen_init(QuickSettingsScreen *panel, QuickSettingsConfig config)
{
    *panel = (QuickSettingsScreen){.config = config};
    panel->model = (ListModel){.context = panel,
                               .count = count,
                               .label = label,
                               .detail = detail,
                               .opens = opens,
                               .detail_below = true};
    panel->screen = (Screen){.title = i18n_text(STR_QUICK_SETTINGS),
                             .context = panel,
                             .enter = enter,
                             .handle_key = handle_key,
                             .update = update,
                             .draw = draw};
    list_view_init(&panel->list, &panel->model, config.theme, 0);
    return &panel->screen;
}
