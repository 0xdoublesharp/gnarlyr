#include "settings_actions.h"
#include "settings_screens.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"

enum {
    MILLISECONDS_PER_MINUTE = 60000
};

const StringId SETTINGS_ACTION_SLEEP_LABELS[SLEEP_TIMER_OPTION_COUNT] = {
    STR_OFF,        STR_MINUTES_15,  STR_MINUTES_30,      STR_MINUTES_45,       STR_MINUTES_60,
    STR_MINUTES_90, STR_MINUTES_120, STR_SLEEP_END_TRACK, STR_SLEEP_END_CHAPTER};

static const Settings *settings_of(const void *context)
{
    const SettingsContext *settings = context;
    return &settings->store->settings;
}

void settings_action_save(SettingsContext *context)
{
    settings_store_capture(context->store, player_status(context->player));
    if (!settings_store_save(context->store)) {
        toast_show(context->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
}

void settings_action_candidate(SettingsContext *context, Settings *next)
{
    SettingsStore *store = context->store;
    Settings saved = store->settings;
    settings_store_capture(store, player_status(context->player));
    *next = store->settings;
    store->settings = saved;
}

bool settings_action_publish(SettingsContext *context, const Settings *next)
{
    SettingsStore *store = context->store;
    Settings saved = store->settings;
    store->settings = *next;
    if (settings_store_save(store)) {
        return true;
    }
    store->settings = saved;
    toast_show(context->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    return false;
}

void settings_action_apply_and_save(SettingsContext *context)
{
    settings_store_apply_hardware(&context->store->settings);
    settings_action_save(context);
}

int settings_action_current_shuffle(const void *context)
{
    const SettingsContext *settings = context;
    return (int)player_status(settings->player).shuffle;
}

void settings_action_choose_shuffle(void *context, int index)
{
    SettingsContext *settings = context;
    Settings next;
    settings_action_candidate(settings, &next);
    next.shuffle = (ShuffleMode)index;
    if (settings_action_publish(settings, &next)) {
        player_set_shuffle(settings->player, (ShuffleMode)index, hal_clock_microseconds());
    }
}

int settings_action_current_repeat(const void *context)
{
    const SettingsContext *settings = context;
    return (int)player_status(settings->player).repeat;
}

void settings_action_choose_repeat(void *context, int index)
{
    SettingsContext *settings = context;
    Settings next;
    settings_action_candidate(settings, &next);
    next.repeat = (RepeatMode)index;
    if (settings_action_publish(settings, &next)) {
        player_set_repeat(settings->player, (RepeatMode)index);
    }
}

int settings_action_current_sleep(const void *context)
{
    const SettingsContext *settings = context;
    return settings->power->sleep_timer_active ? settings->power->sleep_option : 0;
}

void settings_action_choose_sleep(void *context, int index)
{
    SettingsContext *settings = context;
    uint32_t now_ms = hal_clock_milliseconds();
    uint32_t minutes = settings_sleep_option_minutes(index);
    settings->power->sleep_option = (uint8_t)index;
    if (index == 0) {
        power_policy_cancel_sleep_timer(settings->power);
    } else {
        power_policy_start_sleep_timer(settings->power, minutes * MILLISECONDS_PER_MINUTE, now_ms);
    }
    settings->power->sleep_track_end = index >= SLEEP_END_TRACK;
    player_set_stop_after_track(settings->player, index >= SLEEP_END_TRACK);
    StringBuilder message = string_builder_make(settings->message, sizeof settings->message);
    if (index == 0) {
        string_builder_append(&message, i18n_text(STR_SLEEP_OFF));
    } else {
        i18n_format_text(&message, index >= SLEEP_END_TRACK ? STR_SLEEP_AT : STR_SLEEP_IN,
                         i18n_text(SETTINGS_ACTION_SLEEP_LABELS[index]));
    }
    toast_show(settings->toast, settings->message, now_ms);
}

void settings_action_apply_replay_gain(SettingsContext *context)
{
    const Settings *settings = &context->store->settings;
    player_set_replay_gain(context->player, (ReplayGainMode)settings->replay_gain_mode,
                           settings->prevent_clipping);
    settings_action_save(context);
}

int settings_action_current_replay_gain(const void *context)
{
    return settings_of(context)->replay_gain_mode;
}

void settings_action_choose_replay_gain(void *context, int index)
{
    SettingsContext *settings = context;
    Settings next;
    settings_action_candidate(settings, &next);
    next.replay_gain_mode = (uint8_t)index;
    if (settings_action_publish(settings, &next)) {
        player_set_replay_gain(settings->player, (ReplayGainMode)index, next.prevent_clipping);
    }
}

void settings_action_apply_transitions(SettingsContext *context)
{
    const Settings *settings = &context->store->settings;
    player_set_transitions(context->player, settings->track_transition != TRACK_TRANSITION_OFF,
                           settings->transition_tenths);
    settings_action_save(context);
}

int settings_action_current_now_playing(const void *context)
{
    return (int)settings_now_playing_style(settings_of(context));
}

void settings_action_choose_now_playing(void *context, int index)
{
    Settings next;
    settings_action_candidate(context, &next);
    settings_set_now_playing_style(&next, (NowPlayingStyle)index);
    settings_action_publish(context, &next);
}

int settings_action_current_transition(const void *context)
{
    return settings_of(context)->track_transition != TRACK_TRANSITION_OFF ? 1 : 0;
}

void settings_action_choose_transition(void *context, int index)
{
    SettingsContext *settings = context;
    Settings next;
    settings_action_candidate(settings, &next);
    next.track_transition = index != 0 ? TRACK_TRANSITION_FADE : TRACK_TRANSITION_OFF;
    if (!settings_action_publish(settings, &next)) {
        return;
    }
    player_set_transitions(settings->player, index != 0, next.transition_tenths);
    if (index != 0) {
        toast_show(settings->toast, i18n_text(STR_TRANSITIONS_IN_SHUFFLE),
                   hal_clock_milliseconds());
    }
}

int settings_action_current_speed(const void *context)
{
    uint8_t speed = settings_of(context)->playback_speed;
    return speed < SETTINGS_SPEED_COUNT ? speed : SETTINGS_SPEED_NORMAL;
}

void settings_action_choose_speed(void *context, int index)
{
    SettingsContext *settings = context;
    Settings next;
    settings_action_candidate(settings, &next);
    next.playback_speed = (uint8_t)index;
    if (settings_action_publish(settings, &next)) {
        player_set_speed(settings->player, settings_speed_percent(index));
    }
}

bool settings_action_speed_available(const void *context)
{
    const SettingsContext *settings = context;
    return settings->store != NULL && settings->player != NULL;
}
