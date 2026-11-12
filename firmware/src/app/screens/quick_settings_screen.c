#include "quick_settings_screen.h"

#include <stddef.h>

#include "core/i18n.h"
#include "hal/clock.h"
#include "settings_actions.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"

enum {
    SHUFFLE,
    REPEAT,
    EQUALIZER,
    HEADPHONES,
    SPEED,
    SLEEP,
    TRANSITIONS,
    REPLAY_GAIN,
    LYRICS,
    UP_NEXT,
    ROW_COUNT
};

static EqualizerPresets presets;

static const StringId LABELS[] = {
    STR_SHUFFLE,     STR_REPEAT,      STR_EQUALIZER,   STR_HEADPHONES, STR_PLAYBACK_SPEED,
    STR_SLEEP_TIMER, STR_TRANSITIONS, STR_REPLAY_GAIN, STR_LYRICS,     STR_UP_NEXT};
static const StringId SHUFFLE_VALUES[] = {STR_OFF, STR_SHUFFLE_SONGS, STR_SHUFFLE_ALBUMS};
static const StringId REPEAT_VALUES[] = {STR_OFF, STR_REPEAT_MODE_ONE, STR_REPEAT_MODE_ALL};
static const StringId SLEEP_VALUES[] = {
    STR_OFF,        STR_MINUTES_15,  STR_MINUTES_30,      STR_MINUTES_45,       STR_MINUTES_60,
    STR_MINUTES_90, STR_MINUTES_120, STR_SLEEP_END_TRACK, STR_SLEEP_END_CHAPTER};
static const StringId REPLAY_VALUES[] = {STR_OFF, STR_REPLAY_GAIN_TRACK, STR_REPLAY_GAIN_ALBUM,
                                         STR_REPLAY_GAIN_AUTO};

static bool has_speed(const QuickSettingsScreen *panel)
{
    QuickSettingsSpeed speed = panel->config.speed;
    return speed.available != NULL && speed.value != NULL && speed.cycle != NULL &&
           speed.available(speed.context);
}

static int row_of(const QuickSettingsScreen *panel, int index)
{
    return index >= SPEED && !has_speed(panel) ? index + 1 : index;
}

static int count(const void *context)
{
    return ROW_COUNT - (has_speed(context) ? 0 : 1);
}

static const char *label(const void *context, int index)
{
    return i18n_text(LABELS[row_of(context, index)]);
}

static const char *detail(const void *context, int index)
{
    const QuickSettingsScreen *panel = context;
    const SettingsContext *settings = &panel->config.settings;
    switch (row_of(panel, index)) {
    case SHUFFLE:
        return i18n_text(SHUFFLE_VALUES[settings_action_current_shuffle(settings)]);
    case REPEAT:
        return i18n_text(REPEAT_VALUES[settings_action_current_repeat(settings)]);
    case EQUALIZER:
        return settings_action_describe_equalizer(settings,
                                                  settings_action_current_equalizer(settings));
    case HEADPHONES: {
        const HeadphoneProfiles *profiles = &settings->headphones->profiles;
        return profiles->current == HEADPHONE_PROFILE_NONE
                   ? i18n_text(STR_OFF)
                   : profiles->slots[profiles->current].name;
    }
    case SPEED:
        return panel->config.speed.value(panel->config.speed.context);
    case SLEEP:
        return i18n_text(SLEEP_VALUES[settings_action_current_sleep(settings)]);
    case TRANSITIONS:
        return i18n_text(settings_action_current_transition(settings) ? STR_TRANSITION_FADE
                                                                      : STR_OFF);
    case REPLAY_GAIN:
        return i18n_text(REPLAY_VALUES[settings->store->settings.replay_gain_mode]);
    default:
        return NULL;
    }
}

static bool opens(const void *context, int index)
{
    return row_of(context, index) >= LYRICS;
}

static void cycle_headphones(SettingsContext *settings)
{
    HeadphoneProfiles profiles = settings->headphones->profiles;
    headphone_store_fill_slots(&profiles);
    profiles.current = profiles.current == HEADPHONE_PROFILE_NONE ? 0
                       : profiles.current + 1 < profiles.count    ? profiles.current + 1
                                                                  : HEADPHONE_PROFILE_NONE;
    if (!headphone_store_commit(settings->headphones, &profiles, settings->player,
                                settings->store)) {
        toast_show(settings->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
}

static ScreenAction select_row(void *context, int index)
{
    QuickSettingsScreen *panel = context;
    SettingsContext *settings = &panel->config.settings;
    switch (row_of(panel, index)) {
    case SHUFFLE:
        settings_action_choose_shuffle(settings, (settings_action_current_shuffle(settings) + 1) %
                                                     SHUFFLE_MODE_COUNT);
        break;
    case REPEAT:
        settings_action_choose_repeat(settings, (settings_action_current_repeat(settings) + 1) %
                                                    REPEAT_MODE_COUNT);
        break;
    case EQUALIZER:
        settings_action_choose_equalizer(settings,
                                         (settings_action_current_equalizer(settings) + 1) %
                                             (EQUALIZER_PRESET_COUNT + presets.slot_count));
        break;
    case HEADPHONES:
        cycle_headphones(settings);
        break;
    case SPEED:
        panel->config.speed.cycle(panel->config.speed.context);
        break;
    case SLEEP:
        settings_action_choose_sleep(settings, (settings_action_current_sleep(settings) + 1) %
                                                   SLEEP_TIMER_OPTION_COUNT);
        break;
    case TRANSITIONS:
        settings_action_choose_transition(settings, !settings_action_current_transition(settings));
        break;
    case REPLAY_GAIN:
        settings_action_choose_replay_gain(
            settings, (settings->store->settings.replay_gain_mode + 1) % REPLAY_GAIN_MODE_COUNT);
        break;
    case LYRICS:
        return screen_action_push(panel->config.lyrics);
    case UP_NEXT:
        return screen_action_push(panel->config.up_next);
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
    equalizer_presets_scan(&presets);
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
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
               (Label){hint, i18n_text(STR_QUICK_SETTINGS_HINT)});
}

Screen *quick_settings_screen_init(QuickSettingsScreen *panel, QuickSettingsConfig config)
{
    *panel = (QuickSettingsScreen){.config = config};
    panel->config.settings.presets = &presets;
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
