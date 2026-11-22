#include "settings_screens.h"
#include "app/app.h"
#include "audio_settings.h"
#include "missing_fonts.h"
#include "settings_actions.h"

#include <stddef.h>

#include "core/deadline.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "default_theme.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"

enum {
    BRIGHTNESS_LEVEL_COUNT = SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN + 1,
    MILLISECONDS_PER_MINUTE = 60000,
    SAFE_VOLUME_OPTION_COUNT = SETTINGS_SAFE_VOLUME_OPTIONS,
    SAFE_VOLUME_STEP = 6,
    SAFE_VOLUME_TEXT_CAPACITY = 4,
    MODE_ROW_FIRST = PLUG_MODE_COMPUTER,
    MODE_ROW_COUNT = PLUG_MODE_COUNT - MODE_ROW_FIRST,
    ALWAYS_CAPACITY = 64
};

static const StringId SHUFFLE_OPTIONS[SHUFFLE_MODE_COUNT] = {STR_OFF, STR_SHUFFLE_SONGS,
                                                             STR_SHUFFLE_ALBUMS};
static const StringId REPEAT_OPTIONS[REPEAT_MODE_COUNT] = {STR_OFF, STR_REPEAT_MODE_ONE,
                                                           STR_REPEAT_MODE_ALL};
static const StringId AUTO_OFF_OPTIONS[AUTO_OFF_OPTION_COUNT] = {STR_OFF, STR_MINUTES_1,
                                                                 STR_MINUTES_5, STR_MINUTES_10};
static const StringId TIMEOUT_OPTIONS[SCREEN_TIMEOUT_OPTION_COUNT] = {
    STR_SECONDS_10, STR_SECONDS_20, STR_SECONDS_30, STR_MINUTES_1,
    STR_MINUTES_2,  STR_MINUTES_5,  STR_NEVER};
static const StringId CLOCK_FORMAT_OPTIONS[] = {STR_CLOCK_12H, STR_CLOCK_24H};
static const StringId POWER_ON_OPTIONS[] = {STR_POWER_ON_HOLD, STR_POWER_ON_PRESS};
static const char *const BRIGHTNESS_OPTIONS[BRIGHTNESS_LEVEL_COUNT] = {"1", "2", "3", "4", "5",
                                                                       "6", "7", "8", "9", "10"};
static const StringId ACCENT_OPTIONS[ACCENT_COLOR_COUNT] = {
    STR_COLOR_BLUE, STR_COLOR_RED,    STR_COLOR_ORANGE, STR_COLOR_GREEN,
    STR_COLOR_TEAL, STR_COLOR_PURPLE, STR_COLOR_PINK,   STR_COLOR_GRAPHITE};
static const StringId NOW_PLAYING_OPTIONS[] = {STR_NOW_PLAYING_COVER, STR_NOW_PLAYING_CASSETTE,
                                               STR_NOW_PLAYING_VU, STR_NOW_PLAYING_SPECTRUM};
static const StringId THEME_OPTIONS[APPEARANCE_THEME_COUNT] = {
    STR_THEME_GRAPHITE,  STR_THEME_NEON_NOIR, STR_THEME_SUNSET,
    STR_THEME_SEA_GLASS, STR_THEME_CLASSIC,
};
static const StringId TEXT_SIZE_OPTIONS[TEXT_SIZE_COUNT] = {STR_SIZE_SMALL, STR_SIZE_MEDIUM,
                                                            STR_SIZE_LARGE};
static const StringId ORIENTATION_OPTIONS[ORIENTATION_COUNT] = {STR_LANDSCAPE, STR_PORTRAIT,
                                                                STR_PORTRAIT_FLIPPED};
static const StringId GAIN_OPTIONS[GAIN_LEVEL_COUNT] = {STR_GAIN_LOW, STR_GAIN_HIGH};
static const StringId FILTER_OPTIONS[DAC_FILTER_COUNT] = {
    STR_FILTER_FAST_LOW_LATENCY, STR_FILTER_FAST_PHASE_COMPENSATED, STR_FILTER_SLOW_LOW_LATENCY,
    STR_FILTER_SLOW_PHASE_COMPENSATED, STR_FILTER_NON_OVERSAMPLING};
static const StringId OUTPUT_OPTIONS[OUTPUT_JACK_COUNT] = {STR_OUTPUT_AUTO, STR_JACK_35,
                                                           STR_JACK_44};
static const StringId OUTPUT_MESSAGES[OUTPUT_JACK_COUNT] = {STR_OUTPUT_FOLLOWS, STR_OUTPUT_USING_35,
                                                            STR_OUTPUT_USING_44};
static const StringId START_OPTIONS[START_SCREEN_COUNT] = {
    STR_START_MAIN_MENU, STR_NOW_PLAYING, STR_ALBUMS,    STR_ARTISTS,
    STR_FAVORITES,       STR_PLAYLISTS,   STR_COVER_FLOW};
static const StringId USB_ANSWER_OPTIONS[PLUG_MODE_COUNT] = {
    STR_PLUG_ASK, STR_PLUG_COMPUTER, STR_PLUG_CHARGE_PLAY, STR_PLUG_RUN_USB, STR_PLUG_DAC};
static const StringId *const USB_MODE_OPTIONS = USB_ANSWER_OPTIONS + MODE_ROW_FIRST;

static const StringId REPLAY_GAIN_OPTIONS[REPLAY_GAIN_MODE_COUNT] = {
    STR_OFF, STR_REPLAY_GAIN_TRACK, STR_REPLAY_GAIN_ALBUM, STR_REPLAY_GAIN_AUTO};
static const StringId OFF_ON_OPTIONS[] = {STR_OFF, STR_ON};
static const StringId STATUS_LAYOUT_OPTIONS[] = {STR_STATUS_STANDARD, STR_STATUS_COMPACT};

static const StringId BATTERY_STYLE_OPTIONS[] = {STR_BATTERY_SOLID, STR_BATTERY_SEGMENTED};

static const Settings *settings_of(const void *context)
{
    const SettingsContext *settings = context;
    return &settings->store->settings;
}

static Settings *editable_settings(void *context)
{
    SettingsContext *settings = context;
    return &settings->store->settings;
}

static void report(SettingsContext *context, AudioSettingsResult result, StringId not_applied)
{
    if (result != AUDIO_SETTINGS_SAVED) {
        toast_show(
            context->toast,
            i18n_text(result == AUDIO_SETTINGS_NOT_SAVED ? STR_SETTINGS_NOT_SAVED : not_applied),
            hal_clock_milliseconds());
    }
}

static int current_power_on(const void *context)
{
    return (settings_of(context)->power_on_flags >> 4) & 1;
}

static void choose_power_on(void *context, int index)
{
    Settings *settings = editable_settings(context);
    settings->power_on_flags = (uint8_t)((settings->power_on_flags & ~0x10u) | (index ? 0x10u : 0));
    settings_action_save(context);
}

static int current_timeout(const void *context)
{
    return settings_of(context)->screen_timeout;
}

static void choose_timeout(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->screen_timeout = (uint8_t)index;
    power_policy_set_timeout(settings->power, settings_screen_timeout_ms(settings_of(context)));
    settings_action_save(settings);
}

static int current_auto_off(const void *context)
{
    return settings_of(context)->auto_off;
}

static int current_clock_format(const void *context)
{
    return settings_of(context)->clock_24h ? 1 : 0;
}

static void choose_clock_format(void *context, int index)
{
    editable_settings(context)->clock_24h = index != 0;
    settings_action_save(context);
}

static void choose_auto_off(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->auto_off = (uint8_t)index;
    power_policy_set_auto_off(settings->power, settings_auto_off_ms(settings_of(context)),
                              hal_clock_milliseconds());
    settings_action_save(settings);
}

static int current_brightness(const void *context)
{
    return settings_of(context)->brightness - SETTINGS_BRIGHTNESS_MIN;
}

static void choose_brightness(void *context, int index)
{
    editable_settings(context)->brightness = (uint8_t)(index + SETTINGS_BRIGHTNESS_MIN);
    settings_action_apply_and_save(context);
}

static const char *describe_brightness(const void *context, int current)
{
    (void)context;
    return BRIGHTNESS_OPTIONS[current];
}

static void extend_brightness(SettingsScreens *screens, ChoiceConfig *config)
{
    (void)screens;
    config->options = BRIGHTNESS_OPTIONS;
    config->option_ids = NULL;
}

static int current_accent(const void *context)
{
    return (int)settings_of(context)->accent;
}

static void choose_accent(void *context, int index)
{
    editable_settings(context)->accent = (AccentColor)index;
    settings_action_save(context);
}

static int current_appearance(const void *context)
{
    return (int)settings_of(context)->appearance;
}

static void choose_appearance(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->appearance = (AppearanceTheme)index;
    default_theme_set_appearance(settings->theme, (AppearanceTheme)index);
    default_theme_set_accent(settings->theme, settings_of(context)->accent);
    settings_action_save(settings);
}

static int current_text_size(const void *context)
{
    return (int)settings_of(context)->text_size;
}

static void choose_text_size(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->text_size = (TextSize)index;
    default_theme_set_text_size(settings->theme, (TextSize)index);
    settings_action_save(settings);
}

static int current_orientation(const void *context)
{
    return (int)settings_of(context)->orientation;
}

static void choose_orientation(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->orientation = (Orientation)index;
    default_theme_set_orientation(settings->theme, (Orientation)index);
    settings_action_save(settings);
}

static int current_language(const void *context)
{
    const SettingsContext *settings = context;
    return language_list_find(settings->languages, i18n_code());
}

static void choose_language(void *context, int index)
{
    SettingsContext *settings = context;
    if (!language_file_sound(settings->languages->codes[index])) {
        toast_show(settings->toast, i18n_text(STR_LANGUAGE_FAILED), hal_clock_milliseconds());
        return;
    }
    StringBuilder code =
        string_builder_make(editable_settings(context)->language, SETTINGS_LANGUAGE_CAPACITY);
    string_builder_append(&code, settings->languages->codes[index]);
    settings_action_save(settings);
    if (settings->language_requested != NULL) {
        *settings->language_requested = true;
    }
}

static const char *describe_language(const void *context, int current)
{
    (void)context;
    (void)current;
    return i18n_text(STR_LANGUAGE_NAME);
}

static const char *describe_current_language(const void *context)
{
    (void)context;
    return i18n_text(STR_LANGUAGE_NAME);
}

static void extend_language(SettingsScreens *screens, ChoiceConfig *config)
{
    config->options = screens->languages.options;
    config->option_ids = NULL;
    config->option_count = screens->languages.count;
}

static int current_gain(const void *context)
{
    return (int)settings_of(context)->gain;
}

static void choose_gain(void *context, int index)
{
    SettingsContext *settings = context;
    report(settings, audio_settings_set_gain(settings->store, settings->player, (GainLevel)index),
           STR_GAIN_NOT_CHANGED);
}

static int current_filter(const void *context)
{
    return (int)settings_of(context)->dac_filter;
}

static void choose_filter(void *context, int index)
{
    SettingsContext *settings = context;
    report(settings, audio_settings_set_filter(settings->store, settings->player, (DacFilter)index),
           STR_FILTER_NOT_CHANGED);
}

static int current_output(const void *context)
{
    return (int)settings_of(context)->output;
}

static void choose_output(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->output = (OutputJack)index;
    settings_action_apply_and_save(context);
    toast_show(settings->toast, i18n_text(OUTPUT_MESSAGES[index]), hal_clock_milliseconds());
}

static const char *describe_output(const void *context, int current)
{
    (void)context;
    HalAudioJack jack = hal_audio_jack();
    if (current != OUTPUT_AUTO || jack == HAL_AUDIO_JACK_NONE) {
        return i18n_text(OUTPUT_OPTIONS[current]);
    }
    return i18n_text(jack == HAL_AUDIO_JACK_BALANCED ? STR_OUTPUT_AUTO_44 : STR_OUTPUT_AUTO_35);
}

static void choose_equalizer(void *context, int index)
{
    ((SettingsContext *)context)->store->equalizer_slot = -1;
    editable_settings(context)->equalizer = (EqualizerPreset)index;
    settings_action_apply_and_save(context);
}

static const char *describe_equalizer(const void *context, int current)
{
    static const StringId slots[] = {STR_EQ_SLOT_1, STR_EQ_SLOT_2, STR_EQ_SLOT_3, STR_EQ_SLOT_4,
                                     STR_EQ_SLOT_5, STR_EQ_SLOT_6, STR_EQ_SLOT_7, STR_EQ_SLOT_8};
    const SettingsContext *settings = context;
    int slot = settings->store->equalizer_slot;
    if (current == EQUALIZER_PARAMETRIC && settings->store->parametric.description[0] != '\0') {
        return settings->store->parametric.description;
    }
    return slot >= 0 && slot < EQUALIZER_SLOT_COUNT ? i18n_text(slots[slot])
                                                    : i18n_text(EQUALIZER_PRESET_NAMES[current]);
}

static int current_equalizer(const void *context)
{
    return (int)settings_of(context)->equalizer;
}

static int current_start(const void *context)
{
    return (int)settings_of(context)->start_screen;
}

static void choose_start(void *context, int index)
{
    editable_settings(context)->start_screen = (StartScreen)index;
    settings_action_save(context);
}

static void plug_settings(SettingsContext *settings, PlugMode mode, bool always)
{
    Settings *saved = editable_settings(settings);
    if (always) {
        plug_mode_save(saved, mode);
    }
    if (mode == PLUG_MODE_COMPUTER || mode == PLUG_MODE_DAC) {
        saved->usb_mode = mode == PLUG_MODE_DAC ? USB_MODE_DAC : USB_MODE_STORAGE;
    }
}

static void announce_always(SettingsContext *settings, PlugMode mode)
{
    StringBuilder text = string_builder_make(settings->message, sizeof settings->message);
    i18n_format_text(&text, STR_PLUG_ALWAYS, i18n_text(plug_mode_name(mode)));
    toast_show(settings->toast, settings->message, hal_clock_milliseconds());
}

static void choose_plug_mode(void *context, PlugMode mode, bool always)
{
    SettingsContext *settings = context;
    bool connects = mode == PLUG_MODE_COMPUTER || mode == PLUG_MODE_DAC;
    plug_settings(settings, mode, always);
    if (connects || always) {
        settings_action_apply_and_save(settings);
    }
    if (connects) {
        hal_system_request_usb_mode(mode == PLUG_MODE_DAC ? HAL_USB_MODE_DAC
                                                          : HAL_USB_MODE_STORAGE);
    } else {
        hal_power_use_battery_care(mode == PLUG_MODE_USB_POWER ? PLUG_CARE_USB : PLUG_CARE_FULL);
        hal_system_request_usb_mode(HAL_USB_MODE_PLAYER);
    }
    if (always) {
        announce_always(settings, mode);
    }
}

static void choose_saved_plug_mode(void *context, PlugMode mode, bool always)
{
    (void)always;
    plug_mode_save(editable_settings(context), mode);
    settings_action_apply_and_save(context);
}

static int current_usb_mode(const void *context)
{
    (void)context;
    if (!hal_power_battery().usb_power) {
        return -1;
    }
    PlugMode mode =
        hal_power_battery_care() == PLUG_CARE_USB ? PLUG_MODE_USB_POWER : PLUG_MODE_CHARGE;
    return (int)mode - MODE_ROW_FIRST;
}

static void choose_usb_mode(void *context, int index)
{
    choose_plug_mode(context, (PlugMode)(index + MODE_ROW_FIRST), false);
}

static int current_usb_answer(const void *context)
{
    return (int)plug_mode_saved(settings_of(context));
}

static void choose_usb_answer(void *context, int index)
{
    choose_saved_plug_mode(context, (PlugMode)index, false);
}

static int current_prevent_clipping(const void *context)
{
    return settings_of(context)->prevent_clipping ? 1 : 0;
}

static void choose_prevent_clipping(void *context, int index)
{
    editable_settings(context)->prevent_clipping = index != 0;
    settings_action_apply_replay_gain(context);
}

static int current_battery_percent(const void *context)
{
    return settings_of(context)->battery_percent ? 1 : 0;
}

static void choose_battery_percent(void *context, int index)
{
    editable_settings(context)->battery_percent = index != 0;
    settings_action_save(context);
}

static int current_rewind(const void *context)
{
    return settings_of(context)->rewind_on_resume ? 1 : 0;
}

static void choose_rewind(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->rewind_on_resume = index != 0;
    player_set_rewind(settings->player, index != 0);
    settings_action_save(settings);
}

static const char *const SAFE_VOLUME_LEVELS[SAFE_VOLUME_OPTION_COUNT] = {"",   "12", "18",
                                                                         "24", "30", "36"};

static int current_safe_volume(const void *context)
{
    const Settings *settings = settings_of(context);
    int option = (settings->safe_start_volume_level + SAFE_VOLUME_STEP / 2) / SAFE_VOLUME_STEP - 1;
    option = option < 1 ? 1 : option;
    option = option < SAFE_VOLUME_OPTION_COUNT ? option : SAFE_VOLUME_OPTION_COUNT - 1;
    return settings->safe_start_volume ? option : 0;
}

static void choose_safe_volume(void *context, int index)
{
    Settings *settings = editable_settings(context);
    settings->safe_start_volume = index != 0;
    if (index != 0) {
        settings->safe_start_volume_level = (uint8_t)((index + 1) * SAFE_VOLUME_STEP);
    }
    settings_action_save(context);
}

static const char *describe_safe_volume(const void *context, int current)
{
    static char text[SAFE_VOLUME_TEXT_CAPACITY];
    if (current == 0) {
        return i18n_text(STR_OFF);
    }
    StringBuilder level = string_builder_make(text, sizeof text);
    string_builder_append_unsigned(&level, settings_of(context)->safe_start_volume_level);
    return text;
}

static void extend_safe_volume(SettingsScreens *screens, ChoiceConfig *config)
{
    screens->safe_volume_options[0] = i18n_text(STR_OFF);
    for (int index = 1; index < SAFE_VOLUME_OPTION_COUNT; index++) {
        screens->safe_volume_options[index] = SAFE_VOLUME_LEVELS[index];
    }
    config->options = screens->safe_volume_options;
    config->option_ids = NULL;
}

static const StringId TRANSITION_OPTIONS[] = {STR_OFF, STR_TRANSITION_FADE};
static const StringId TRANSITION_LENGTH_OPTIONS[] = {
    STR_SECONDS_0_5, STR_SECONDS_1, STR_SECONDS_1_5, STR_SECONDS_2,
    STR_SECONDS_3,   STR_SECONDS_4, STR_SECONDS_6};
static const uint8_t TRANSITION_TENTHS[] = {5, 10, 15, 20, 30, 40, 60};

static int current_transition_length(const void *context)
{
    uint8_t tenths = settings_of(context)->transition_tenths;
    int chosen = 0;
    for (int index = 1; index < (int)sizeof TRANSITION_TENTHS; index++) {
        chosen = TRANSITION_TENTHS[index] <= tenths ? index : chosen;
    }
    return chosen;
}

static void choose_transition_length(void *context, int index)
{
    editable_settings(context)->transition_tenths = TRANSITION_TENTHS[index];
    settings_action_apply_transitions(context);
}

static bool transition_length_shown(const void *context)
{
    return settings_action_current_transition(context) != 0;
}

static int current_status_layout(const void *context)
{
    return settings_of(context)->status_bar_compact;
}

static void choose_status_layout(void *context, int index)
{
    editable_settings(context)->status_bar_compact = index != 0;
    settings_action_save(context);
}

static int current_status_item(const void *context, uint8_t item)
{
    return (settings_of(context)->status_items & item) != 0;
}

static void choose_status_item(void *context, int index, uint8_t item)
{
    Settings *settings = editable_settings(context);
    settings->status_items = (uint8_t)((settings->status_items & ~item) | (index != 0 ? item : 0));
    settings_action_save(context);
}

static int current_status_volume(const void *context)
{
    return current_status_item(context, SETTINGS_STATUS_VOLUME);
}

static void choose_status_volume(void *context, int index)
{
    choose_status_item(context, index, SETTINGS_STATUS_VOLUME);
}

static int current_status_repeat(const void *context)
{
    return current_status_item(context, SETTINGS_STATUS_REPEAT);
}

static void choose_status_repeat(void *context, int index)
{
    choose_status_item(context, index, SETTINGS_STATUS_REPEAT);
}

static int current_status_position(const void *context)
{
    return current_status_item(context, SETTINGS_STATUS_POSITION);
}

static void choose_status_position(void *context, int index)
{
    choose_status_item(context, index, SETTINGS_STATUS_POSITION);
}

static int current_status_style(const void *context)
{
    return current_status_item(context, SETTINGS_STATUS_SEGMENTED);
}

static void choose_status_style(void *context, int index)
{
    choose_status_item(context, index, SETTINGS_STATUS_SEGMENTED);
}

static const StringId CROSSFEED_OPTIONS[] = {STR_OFF, STR_CROSSFEED_LOW, STR_CROSSFEED_MEDIUM,
                                             STR_CROSSFEED_HIGH};

static int current_skip_silence(const void *context)
{
    return settings_of(context)->skip_silence ? 1 : 0;
}

static void choose_skip_silence(void *context, int index)
{
    editable_settings(context)->skip_silence = index != 0;
    settings_action_save(context);
}

static int current_speech_boost(const void *context)
{
    return settings_of(context)->speech_boost ? 1 : 0;
}

static void choose_speech_boost(void *context, int index)
{
    editable_settings(context)->speech_boost = index != 0;
    settings_action_save(context);
}

static int current_crossfeed(const void *context)
{
    return settings_of(context)->crossfeed;
}

static void choose_crossfeed(void *context, int index)
{
    editable_settings(context)->crossfeed = (uint8_t)index;
    settings_action_save(context);
}

static int current_standby(const void *context)
{
    return settings_of(context)->standby_enabled;
}

static void choose_standby(void *context, int index)
{
    editable_settings(context)->standby_enabled = index != 0;
    settings_action_save(context);
}

static int current_startup_animation(const void *context)
{
    return settings_of(context)->startup_animation;
}

static void choose_startup_animation(void *context, int index)
{
    editable_settings(context)->startup_animation = index != 0;
    settings_action_save(context);
}

static int current_standby_timeout(const void *context)
{
    return settings_of(context)->standby_power_off_option;
}

static void choose_standby_timeout(void *context, int index)
{
    editable_settings(context)->standby_power_off_option = (uint8_t)index;
    settings_action_save(context);
}

static const StringId STANDBY_OPTIONS[] = {STR_MINUTES_30, STR_STANDBY_HOURS_2,
                                           STR_STANDBY_HOURS_8};

static const SettingChoice CHOICES[SETTINGS_CHOICE_COUNT] = {
    {STR_SHUFFLE, SHUFFLE_OPTIONS, SHUFFLE_MODE_COUNT, settings_action_current_shuffle,
     settings_action_choose_shuffle, NULL, NULL},
    {STR_REPEAT, REPEAT_OPTIONS, REPEAT_MODE_COUNT, settings_action_current_repeat,
     settings_action_choose_repeat, NULL, NULL},
    {STR_SLEEP_TIMER, SETTINGS_ACTION_SLEEP_LABELS, SLEEP_TIMER_OPTION_COUNT,
     settings_action_current_sleep, settings_action_choose_sleep, NULL, NULL},
    {STR_AUTO_OFF, AUTO_OFF_OPTIONS, AUTO_OFF_OPTION_COUNT, current_auto_off, choose_auto_off, NULL,
     NULL},
    {STR_SCREEN_TIMEOUT, TIMEOUT_OPTIONS, SCREEN_TIMEOUT_OPTION_COUNT, current_timeout,
     choose_timeout, NULL, NULL},
    {STR_CLOCK_FORMAT, CLOCK_FORMAT_OPTIONS, 2, current_clock_format, choose_clock_format, NULL,
     NULL},
    {STR_BRIGHTNESS, NULL, BRIGHTNESS_LEVEL_COUNT, current_brightness, choose_brightness,
     describe_brightness, extend_brightness},
    {STR_COLOR, ACCENT_OPTIONS, ACCENT_COLOR_COUNT, current_accent, choose_accent, NULL, NULL},
    {STR_TEXT_SIZE, TEXT_SIZE_OPTIONS, TEXT_SIZE_COUNT, current_text_size, choose_text_size, NULL,
     NULL},
    {STR_ORIENTATION, ORIENTATION_OPTIONS, ORIENTATION_COUNT, current_orientation,
     choose_orientation, NULL, NULL},
    [SETTINGS_LANGUAGE_CHOICE] = {STR_LANGUAGE, NULL, 0, current_language, choose_language,
                                  describe_language, extend_language},
    {STR_GAIN, GAIN_OPTIONS, GAIN_LEVEL_COUNT, current_gain, choose_gain, NULL, NULL},
    {STR_DAC_FILTER, FILTER_OPTIONS, DAC_FILTER_COUNT, current_filter, choose_filter, NULL, NULL},
    {STR_OUTPUT, OUTPUT_OPTIONS, OUTPUT_JACK_COUNT, current_output, choose_output, describe_output,
     NULL},
    {STR_EQUALIZER, EQUALIZER_PRESET_NAMES, EQUALIZER_PRESET_COUNT, current_equalizer,
     choose_equalizer, describe_equalizer, NULL},
    {STR_REPLAY_GAIN, REPLAY_GAIN_OPTIONS, REPLAY_GAIN_MODE_COUNT,
     settings_action_current_replay_gain, settings_action_choose_replay_gain, NULL, NULL},
    {STR_PREVENT_CLIPPING, OFF_ON_OPTIONS, 2, current_prevent_clipping, choose_prevent_clipping,
     NULL, NULL},
    {STR_START_SCREEN, START_OPTIONS, START_SCREEN_COUNT, current_start, choose_start, NULL, NULL},
    {STR_USB_MODE, USB_MODE_OPTIONS, MODE_ROW_COUNT, current_usb_mode, choose_usb_mode, NULL, NULL},
    {STR_THEME, THEME_OPTIONS, APPEARANCE_THEME_COUNT, current_appearance, choose_appearance, NULL,
     NULL},
    {STR_BATTERY_PERCENTAGE, OFF_ON_OPTIONS, 2, current_battery_percent, choose_battery_percent,
     NULL, NULL},
    {STR_PLAYBACK_SPEED, SETTINGS_SPEED_LABELS, SETTINGS_SPEED_COUNT, settings_action_current_speed,
     settings_action_choose_speed, NULL, NULL},
    {STR_REWIND_ON_RESUME, OFF_ON_OPTIONS, 2, current_rewind, choose_rewind, NULL, NULL},
    {STR_SAFE_START_VOLUME, NULL, SAFE_VOLUME_OPTION_COUNT, current_safe_volume, choose_safe_volume,
     describe_safe_volume, extend_safe_volume},
    {STR_TRANSITIONS, TRANSITION_OPTIONS, 2, settings_action_current_transition,
     settings_action_choose_transition, NULL, NULL},
    {STR_TRANSITION_LENGTH, TRANSITION_LENGTH_OPTIONS, (int)sizeof TRANSITION_TENTHS,
     current_transition_length, choose_transition_length, NULL, NULL},
    {STR_USB_ANSWER, USB_ANSWER_OPTIONS, PLUG_MODE_COUNT, current_usb_answer, choose_usb_answer,
     NULL, NULL},
    {STR_POWER_ON, POWER_ON_OPTIONS, 2, current_power_on, choose_power_on, NULL, NULL},
    {STR_SKIP_SILENCE, OFF_ON_OPTIONS, 2, current_skip_silence, choose_skip_silence, NULL, NULL},
    {STR_SPEECH_BOOST, OFF_ON_OPTIONS, 2, current_speech_boost, choose_speech_boost, NULL, NULL},
    {STR_CROSSFEED, CROSSFEED_OPTIONS, HAL_AUDIO_CROSSFEED_LEVELS, current_crossfeed,
     choose_crossfeed, NULL, NULL},
    {STR_STATUS_VOLUME, OFF_ON_OPTIONS, 2, current_status_volume, choose_status_volume, NULL, NULL},
    {STR_STATUS_REPEAT, OFF_ON_OPTIONS, 2, current_status_repeat, choose_status_repeat, NULL, NULL},
    {STR_STATUS_POSITION, OFF_ON_OPTIONS, 2, current_status_position, choose_status_position, NULL,
     NULL},
    {STR_BATTERY_STYLE, BATTERY_STYLE_OPTIONS, 2, current_status_style, choose_status_style, NULL,
     NULL},
    {STR_STATUS_LAYOUT, STATUS_LAYOUT_OPTIONS, 2, current_status_layout, choose_status_layout, NULL,
     NULL},
    {STR_STANDBY_ENABLED, OFF_ON_OPTIONS, 2, current_standby, choose_standby, NULL, NULL},
    {STR_STANDBY_TIMEOUT, STANDBY_OPTIONS, 3, current_standby_timeout, choose_standby_timeout, NULL,
     NULL},
    {STR_STARTUP_ANIMATION, OFF_ON_OPTIONS, 2, current_startup_animation, choose_startup_animation,
     NULL, NULL},
    [SETTINGS_NOW_PLAYING_CHOICE] = {STR_NOW_PLAYING_STYLE, NOW_PLAYING_OPTIONS, 4,
                                     settings_action_current_now_playing,
                                     settings_action_choose_now_playing, NULL, NULL},
};

static PlugState plug_state(const SettingsScreens *screens, PlugPlace place)
{
    return (PlugState){.place = place,
                       .saved = plug_mode_saved(&screens->context.store->settings),
                       .care = hal_power_battery_care(),
                       .battery = hal_power_battery()};
}

static Screen *saved_plug_view(SettingsScreens *screens)
{
    PlugChooserConfig config = {.state = plug_state(screens, PLUG_PLACE_SETTINGS),
                                .title = i18n_text(STR_USB_ANSWER),
                                .choose = choose_saved_plug_mode,
                                .context = &screens->context,
                                .theme = screens->theme};
    return plug_chooser_init(&screens->plug, config);
}

static Screen *choice_view(SettingsScreens *screens, const SettingChoice *choice,
                           ChoiceConfig config)
{
    if (choice->title == STR_USB_MODE) {
        return settings_screens_usb_modes(screens, false);
    }
    if (choice->title == STR_USB_ANSWER) {
        return saved_plug_view(screens);
    }
    return choice_screen_init(&screens->choice, config);
}

static ScreenAction open_choice(void *context)
{
    SettingsEntry *entry = context;
    SettingsScreens *screens = entry->owner;
    const SettingChoice *choice = entry->choice;
    if (choice->title == STR_EQUALIZER) {
        return screen_action_push(screens->context.equalizer);
    }
    screens->choice_preview = choice->title;
    ChoiceConfig config = {
        .title = i18n_text(choice->title),
        .option_ids = choice->options,
        .option_count = choice->option_count,
        .current = choice->current,
        .choose = choice->choose,
        .context = &screens->context,
        .theme = screens->theme,
    };
    if (choice->extend != NULL) {
        choice->extend(screens, &config);
    }
    return screen_action_push(choice_view(screens, choice, config));
}

static const char *choice_value(const void *context);

static ScreenAction open_power(void *context)
{
    SettingsScreens *screens = context;
    for (int index = 0; index < SETTINGS_POWER_CHOICES; index++) {
        screens->power_items[index] =
            (MenuItem){.label = i18n_text(CHOICES[SETTINGS_STANDBY_CHOICE + index].title),
                       .select = open_choice,
                       .context = &screens->entries[SETTINGS_STANDBY_CHOICE + index],
                       .value = choice_value,
                       .opens = true};
    }
    MenuConfig config = {.title = i18n_text(STR_STANDBY_POWER),
                         .items = screens->power_items,
                         .item_count = SETTINGS_POWER_CHOICES,
                         .theme = screens->theme,
                         .detail_below = true};
    return screen_action_push(menu_screen_init(&screens->power_menu, config));
}

Screen *settings_screens_usb_modes(SettingsScreens *screens, bool prompt)
{
    PlugPlace place = prompt ? PLUG_PLACE_PROMPT : PLUG_PLACE_SWITCH;
    PlugChooserConfig config = {.state = plug_state(screens, place),
                                .title = i18n_text(prompt ? STR_PLUG_TITLE : STR_USB_MODE),
                                .choose = choose_plug_mode,
                                .context = &screens->context,
                                .theme = screens->theme};
    return plug_chooser_init(&screens->usb, config);
}

static const char *choice_value(const void *context)
{
    const SettingsEntry *entry = context;
    int current = entry->choice->current(&entry->owner->context);
    if (current < 0) {
        return "";
    }
    if (entry->choice->describe != NULL) {
        return entry->choice->describe(&entry->owner->context, current);
    }
    if (current >= entry->choice->option_count) {
        return "";
    }
    return i18n_text(entry->choice->options[current]);
}

#ifdef ECHO_DEBUG_LOOP
const SettingsChooseTables SETTINGS_CHOOSE_TABLES = {CHOICES, &BUTTON_CHOICES};
#endif

static ScreenAction request_rescan(void *context)
{
    SettingsContext *settings = context;
    if (settings->rescan_requested != NULL) {
        *settings->rescan_requested = true;
    }
    return screen_action_none();
}

static ScreenAction open_clock(void *context)
{
    SettingsScreens *screens = context;
    screens->choice_preview = STRING_COUNT;
    SetClockConfig config = {screens->theme, &screens->context.store->settings,
                             screens->context.toast};
    return screen_action_push(set_clock_screen_init(&screens->clock, config));
}

static const char *clock_summary(const void *context)
{
    const SettingsScreens *screens = context;
    return set_clock_screen_summary(&screens->context.store->settings);
}

static const char *format_status(const void *context)
{
    const SettingsScreens *screens = context;
    return screens->card_needs_format ? i18n_text(STR_CARD_FORMAT_NEEDED) : NULL;
}

static ScreenAction open_format(void *context)
{
    SettingsScreens *screens = context;
    return screen_action_push(screens->context.card_format);
}

static bool card_mounted(const void *context)
{
    (void)context;
    HalCardInfo card = hal_card_info();
    return card.mounted && !card.usb_busy;
}

static bool card_ejected(const void *context)
{
    (void)context;
    HalCardInfo card = hal_card_info();
    return card.ejected && card.present && !card.usb_busy;
}

static ScreenAction change_card(void *context)
{
    SettingsScreens *screens = context;
    bool eject = hal_card_info().mounted;
    if (screens->context.player->state == PLAYER_PLAYING) {
        player_toggle_pause(screens->context.player);
    }
    if (screens->context.format_prepare != NULL) {
        screens->context.format_prepare();
    }
    hal_audio_release();
    hal_card_eject(eject);
    return screen_action_none();
}

static void buttons_changed(void *context)
{
    settings_action_save(context);
}

static bool headphones_shown(const void *context)
{
    const SettingsScreens *screens = context;
    return screens->context.headphones != NULL;
}

static void bind_headphones(SettingsScreens *screens)
{
    SettingsContext *context = &screens->context;
    Screen *target = NULL;
    if (context->headphones != NULL) {
        HeadphoneEditorConfig config = {context->headphones, context->player, context->store,
                                        screens->theme, context->toast};
        target = headphone_editor_init(&screens->headphones, config);
    }
    screens->items[SETTINGS_HEADPHONES_ITEM] = (MenuItem){.label = i18n_text(STR_HEADPHONES),
                                                          .select = menu_open_screen,
                                                          .context = target,
                                                          .opens = true,
                                                          .shown = headphones_shown,
                                                          .shown_context = screens};
}

static ScreenAction open_status_bar(void *context);

static void bind_status_bar(SettingsScreens *screens)
{
    for (int index = 0; index < SETTINGS_STATUS_COUNT; index++) {
        int choice = index == 3   ? SETTINGS_STATUS_CHOICE
                     : index >= 4 ? SETTINGS_MAIN_CHOICE_COUNT + 3 + index - 4
                                  : SETTINGS_MAIN_CHOICE_COUNT + index;
        screens->status_items[index] = (MenuItem){.label = i18n_text(CHOICES[choice].title),
                                                  .select = open_choice,
                                                  .context = &screens->entries[choice],
                                                  .value = choice_value,
                                                  .opens = true};
    }
    MenuConfig config = {.title = i18n_text(STR_STATUS_BAR),
                         .items = screens->status_items,
                         .item_count = SETTINGS_STATUS_COUNT,
                         .theme = screens->theme,
                         .detail_below = true};
    menu_screen_init(&screens->status_menu, config);
    screens->items[SETTINGS_STATUS_BAR_ITEM] = (MenuItem){.label = i18n_text(STR_STATUS_BAR),
                                                          .select = open_status_bar,
                                                          .context = screens,
                                                          .opens = true};
}

static ScreenAction open_status_bar(void *context)
{
    SettingsScreens *screens = context;
    bind_status_bar(screens);
    return screen_action_push(&screens->status_menu.screen);
}

enum {
    ROW_SPECIAL = -1,
    LIST_GROUPS = SETTINGS_GROUP_LIBRARY + 1
};

static const int8_t ROW_CHOICES[SETTINGS_ITEM_COUNT] = {
    13,          11,          14,          ROW_SPECIAL,
    ROW_SPECIAL, ROW_SPECIAL, 30,          15,
    16,          12,          0,           1,
    21,          24,          25,          22,
    28,          29,          23,          2,
    8,           9,           19,          7,
    6,           4,           ROW_SPECIAL, SETTINGS_NOW_PLAYING_CHOICE,
    ROW_SPECIAL, 17,          5,           ROW_SPECIAL,
    3,           ROW_SPECIAL, 27,          26,
    18,          ROW_SPECIAL, ROW_SPECIAL, ROW_SPECIAL,
    ROW_SPECIAL, ROW_SPECIAL, ROW_SPECIAL, ROW_SPECIAL};

static const uint8_t GROUP_FIRST[LIST_GROUPS + 1] = {
    SETTINGS_SOUND_FIRST,   SETTINGS_PLAYBACK_FIRST, SETTINGS_DISPLAY_FIRST,
    SETTINGS_POWER_FIRST,   SETTINGS_POWER_FIRST,    SETTINGS_CONNECTIONS_FIRST,
    SETTINGS_LIBRARY_FIRST, SETTINGS_LIBRARY_FIRST,  SETTINGS_ITEM_COUNT};

static const StringId GROUP_TITLES[SETTINGS_GROUP_COUNT] = {
    STR_SETTINGS_SOUND,   STR_SETTINGS_PLAYBACK,      STR_SETTINGS_DISPLAY_TEXT,
    STR_LANGUAGE,         STR_SETTINGS_POWER_BATTERY, STR_SETTINGS_CONNECTIONS,
    STR_SETTINGS_BUTTONS, STR_SETTINGS_LIBRARY,       STR_ABOUT};

static const IconId GROUP_ICONS[SETTINGS_GROUP_COUNT] = {
    ICON_AUDIO, ICON_NOW_PLAYING, ICON_COMPUTER, ICON_GLOBE, ICON_POWER,
    ICON_USB,   ICON_CONTROLS,    ICON_ALBUM,    ICON_LOGO};

static ScreenAction open_group(void *context)
{
    SettingsGroupEntry *entry = context;
    SettingsScreens *screens = entry->owner;
    int first = GROUP_FIRST[entry->group];
    MenuConfig config = {.title = i18n_text(GROUP_TITLES[entry->group]),
                         .items = screens->items + first,
                         .item_count = GROUP_FIRST[entry->group + 1] - first,
                         .theme = screens->theme,
                         .detail_below = true};
    return screen_action_push(menu_screen_init(&screens->group_menu, config));
}

static ScreenAction open_language(void *context)
{
    SettingsScreens *screens = context;
    language_list(&screens->languages);
    LanguagePickerConfig config = {.languages = &screens->languages,
                                   .choose = choose_language,
                                   .context = &screens->context,
                                   .theme = screens->theme};
    return screen_action_push(language_picker_init(&screens->language, config));
}

static ScreenAction open_about(void *context)
{
    SettingsScreens *screens = context;
    uint32_t names = missing_fonts_names(screens->theme->fonts.body);
    StringBuilder notice = string_builder_make(screens->missing_notice, SETTINGS_NOTICE_CAPACITY);
    if (names > 0) {
        i18n_format_counts(&notice, names == 1 ? STR_NAMES_NEED_FONTS_ONE : STR_NAMES_NEED_FONTS,
                           names, 0);
    }
    screens->missing_names = names;
    return screens->about != NULL ? screen_action_push(screens->about) : screen_action_none();
}

static const char *text_size_value(const void *context)
{
    const SettingsGroupEntry *entry = context;
    return choice_value(&entry->owner->entries[SETTINGS_TEXT_SIZE_CHOICE]);
}

static void bind_choice_rows(SettingsScreens *screens)
{
    for (int index = 0; index < SETTINGS_CHOICE_COUNT; index++) {
        screens->entries[index] = (SettingsEntry){.choice = &CHOICES[index], .owner = screens};
    }
    for (int item = 0; item < SETTINGS_ITEM_COUNT; item++) {
        int choice = ROW_CHOICES[item];
        if (choice != ROW_SPECIAL) {
            screens->items[item] = (MenuItem){.label = i18n_text(CHOICES[choice].title),
                                              .select = open_choice,
                                              .context = &screens->entries[choice],
                                              .value = choice_value,
                                              .opens = true};
        }
    }
    screens->items[SETTINGS_TRANSITION_LENGTH_ITEM].shown = transition_length_shown;
    screens->items[SETTINGS_TRANSITION_LENGTH_ITEM].shown_context = &screens->context;
}

static void bind_groups(SettingsScreens *screens, Screen *buttons)
{
    for (int group = 0; group < SETTINGS_GROUP_COUNT; group++) {
        screens->group_entries[group] = (SettingsGroupEntry){screens, (SettingsGroup)group};
        screens->groups[group] = (MenuItem){.label = i18n_text(GROUP_TITLES[group]),
                                            .select = open_group,
                                            .context = &screens->group_entries[group],
                                            .opens = true,
                                            .icon = GROUP_ICONS[group]};
    }
    screens->groups[SETTINGS_GROUP_DISPLAY].value = text_size_value;
    screens->groups[SETTINGS_GROUP_LANGUAGE].select = open_language;
    screens->groups[SETTINGS_GROUP_LANGUAGE].context = screens;
    screens->groups[SETTINGS_GROUP_LANGUAGE].value = describe_current_language;
    screens->groups[SETTINGS_GROUP_BUTTONS].select = menu_open_screen;
    screens->groups[SETTINGS_GROUP_BUTTONS].context = buttons;
    screens->groups[SETTINGS_GROUP_ABOUT].select = open_about;
    screens->groups[SETTINGS_GROUP_ABOUT].context = screens;
}

static void bind_card_rows(SettingsScreens *screens)
{
    screens->items[SETTINGS_EJECT_ITEM] = (MenuItem){.label = i18n_text(STR_CARD_EJECT),
                                                     .select = change_card,
                                                     .context = screens,
                                                     .shown = card_mounted};
    screens->items[SETTINGS_MOUNT_ITEM] = (MenuItem){.label = i18n_text(STR_CARD_MOUNT),
                                                     .select = change_card,
                                                     .context = screens,
                                                     .shown = card_ejected};
    screens->card_needs_format = hal_card_info().needs_format;
    screens->items[SETTINGS_FORMAT_ITEM] = (MenuItem){.label = i18n_text(STR_CARD_FORMAT),
                                                      .select = open_format,
                                                      .value = format_status,
                                                      .context = screens,
                                                      .opens = true};
    screens->items[SETTINGS_RESCAN_ITEM] = (MenuItem){.label = i18n_text(STR_RESCAN_LIBRARY),
                                                      .select = request_rescan,
                                                      .context = &screens->context};
    screens->items[SETTINGS_HIDDEN_ITEM] = hidden_choice_item(screens);
    screens->items[SETTINGS_RESTORE_ITEM] = settings_backup_item(screens);
}

static MenuItem opening(StringId label, ScreenAction (*select)(void *), void *context)
{
    return (MenuItem){
        .label = i18n_text(label), .select = select, .context = context, .opens = true};
}

static void bind_special_rows(SettingsScreens *screens)
{
    SettingsContext *context = &screens->context;
    bind_card_rows(screens);
    bind_headphones(screens);
    screens->items[SETTINGS_DATE_TIME_ITEM] = opening(STR_SET_CLOCK, open_clock, screens);
    screens->items[SETTINGS_DATE_TIME_ITEM].value = clock_summary;
    screens->items[SETTINGS_BLUETOOTH_ITEM] =
        opening(STR_BLUETOOTH, bluetooth_screen_open, context->bluetooth);
    screens->items[SETTINGS_BLUETOOTH_ITEM].value = bluetooth_screen_summary;
    screens->items[SETTINGS_CUSTOM_EQ_ITEM] =
        opening(STR_CUSTOM_EQ, menu_open_screen, context->graphic);
    screens->items[SETTINGS_PARAMETRIC_ITEM] =
        opening(STR_PEQ, menu_open_screen, context->parametric);
    screens->items[SETTINGS_POWER_ITEM] = opening(STR_STANDBY_POWER, open_power, screens);
    screens->items[SETTINGS_HOME_ITEM] = opening(STR_HOME_SCREEN, NULL, NULL);
    bind_status_bar(screens);
}

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context, const Theme *theme)
{
    screens->context = context;
    screens->context.languages = &screens->languages;
    screens->theme = theme;
    screens->choice_preview = STRING_COUNT;
    screens->preview = (SettingsPreview){.choice = STRING_COUNT};
    screens->preview_settings = settings_preview_shown;
    language_list(&screens->languages);
    ButtonSettingsConfig buttons = {.settings = &screens->context.store->settings,
                                    .theme = theme,
                                    .changed = buttons_changed,
                                    .context = &screens->context};
    Screen *controls = button_settings_init(&screens->buttons, buttons);
    bind_choice_rows(screens);
    bind_special_rows(screens);
    bind_groups(screens, controls);
    MenuConfig menu = {.title = i18n_text(STR_SETTINGS),
                       .items = screens->groups,
                       .item_count = SETTINGS_GROUP_COUNT,
                       .theme = theme};
    return menu_screen_init(&screens->menu, menu);
}

ScreenAction settings_screens_sleep(SettingsScreens *screens)
{
    return open_choice(&screens->entries[2]);
}
