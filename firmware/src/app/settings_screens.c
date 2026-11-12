#include "settings_screens.h"
#include "app/app.h"
#include "audio_settings.h"
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
    SAFE_VOLUME_OPTION_COUNT = 6,
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
static const StringId SLEEP_OPTIONS[SLEEP_TIMER_OPTION_COUNT] = {
    STR_OFF,        STR_MINUTES_15,  STR_MINUTES_30,      STR_MINUTES_45,       STR_MINUTES_60,
    STR_MINUTES_90, STR_MINUTES_120, STR_SLEEP_END_TRACK, STR_SLEEP_END_CHAPTER};
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
static const StringId THEME_OPTIONS[APPEARANCE_THEME_COUNT] = {
    STR_THEME_GRAPHITE,  STR_THEME_NEON_NOIR, STR_THEME_SUNSET,
    STR_THEME_SEA_GLASS, STR_THEME_CLASSIC,
};
static const StringId TEXT_SIZE_OPTIONS[TEXT_SIZE_COUNT] = {STR_SIZE_SMALL, STR_SIZE_MEDIUM,
                                                            STR_SIZE_LARGE};
static const StringId ORIENTATION_OPTIONS[ORIENTATION_COUNT] = {STR_LANDSCAPE, STR_PORTRAIT,
                                                                STR_PORTRAIT_FLIPPED};
static const StringId SKIP_OPTIONS[SKIP_GESTURE_COUNT] = {STR_SKIP_HOLD, STR_SKIP_DOUBLE,
                                                          STR_SKIP_PLAY};
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

static void report_unsaved(SettingsContext *context, bool saved)
{
    if (!saved) {
        toast_show(context->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
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

static void extend_language(SettingsScreens *screens, ChoiceConfig *config)
{
    config->options = screens->languages.options;
    config->option_ids = NULL;
    config->option_count = screens->languages.count;
}

static bool language_shown(const void *context)
{
    const LanguageList *languages = context;
    return languages->count > 1;
}

static int current_skip(const void *context)
{
    return (int)settings_of(context)->skip_gesture;
}

static void choose_skip(void *context, int index)
{
    editable_settings(context)->skip_gesture = (SkipGesture)index;
    settings_action_save(context);
}

static int current_gain(const void *context)
{
    return (int)settings_of(context)->gain;
}

static void choose_gain(void *context, int index)
{
    SettingsContext *settings = context;
    report_unsaved(settings,
                   audio_settings_set_gain(settings->store, settings->player, (GainLevel)index));
}

static int current_filter(const void *context)
{
    return (int)settings_of(context)->dac_filter;
}

static void choose_filter(void *context, int index)
{
    SettingsContext *settings = context;
    report_unsaved(settings,
                   audio_settings_set_filter(settings->store, settings->player, (DacFilter)index));
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

static int current_replay_gain(const void *context)
{
    return settings_of(context)->replay_gain_mode;
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
static const char *safe_volume_options[SAFE_VOLUME_OPTION_COUNT];

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
    (void)screens;
    safe_volume_options[0] = i18n_text(STR_OFF);
    for (int index = 1; index < SAFE_VOLUME_OPTION_COUNT; index++) {
        safe_volume_options[index] = SAFE_VOLUME_LEVELS[index];
    }
    config->options = safe_volume_options;
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

static int current_status_volume(const void *context)
{
    return (settings_of(context)->status_items & SETTINGS_STATUS_VOLUME) != 0;
}

static void choose_status_volume(void *context, int index)
{
    Settings *settings = editable_settings(context);
    settings->status_items = (uint8_t)((settings->status_items & ~SETTINGS_STATUS_VOLUME) |
                                       (index != 0 ? SETTINGS_STATUS_VOLUME : 0));
    settings_action_save(context);
}

static int current_status_repeat(const void *context)
{
    return (settings_of(context)->status_items & SETTINGS_STATUS_REPEAT) != 0;
}

static void choose_status_repeat(void *context, int index)
{
    Settings *settings = editable_settings(context);
    settings->status_items = (uint8_t)((settings->status_items & ~SETTINGS_STATUS_REPEAT) |
                                       (index != 0 ? SETTINGS_STATUS_REPEAT : 0));
    settings_action_save(context);
}

static int current_status_position(const void *context)
{
    return (settings_of(context)->status_items & SETTINGS_STATUS_POSITION) != 0;
}

static void choose_status_position(void *context, int index)
{
    Settings *settings = editable_settings(context);
    settings->status_items = (uint8_t)((settings->status_items & ~SETTINGS_STATUS_POSITION) |
                                       (index != 0 ? SETTINGS_STATUS_POSITION : 0));
    settings_action_save(context);
}

static int current_status_style(const void *context)
{
    return (settings_of(context)->status_items & SETTINGS_STATUS_SEGMENTED) != 0;
}

static void choose_status_style(void *context, int index)
{
    Settings *settings = editable_settings(context);
    settings->status_items = (uint8_t)((settings->status_items & ~SETTINGS_STATUS_SEGMENTED) |
                                       (index != 0 ? SETTINGS_STATUS_SEGMENTED : 0));
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
    {STR_SLEEP_TIMER, SLEEP_OPTIONS, SLEEP_TIMER_OPTION_COUNT, settings_action_current_sleep,
     settings_action_choose_sleep, NULL, NULL},
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
    {STR_SKIP_SONGS, SKIP_OPTIONS, SKIP_GESTURE_COUNT, current_skip, choose_skip, NULL, NULL},
    {STR_GAIN, GAIN_OPTIONS, GAIN_LEVEL_COUNT, current_gain, choose_gain, NULL, NULL},
    {STR_DAC_FILTER, FILTER_OPTIONS, DAC_FILTER_COUNT, current_filter, choose_filter, NULL, NULL},
    {STR_OUTPUT, OUTPUT_OPTIONS, OUTPUT_JACK_COUNT, current_output, choose_output, describe_output,
     NULL},
    {STR_EQUALIZER, EQUALIZER_PRESET_NAMES, EQUALIZER_PRESET_COUNT, current_equalizer,
     choose_equalizer, describe_equalizer, NULL},
    {STR_REPLAY_GAIN, REPLAY_GAIN_OPTIONS, REPLAY_GAIN_MODE_COUNT, current_replay_gain,
     settings_action_choose_replay_gain, NULL, NULL},
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
    for (int index = 0; index < 2; index++) {
        screens->power_items[index] =
            (MenuItem){.label = i18n_text(CHOICES[SETTINGS_STANDBY_CHOICE + index].title),
                       .select = open_choice,
                       .context = &screens->entries[SETTINGS_STANDBY_CHOICE + index],
                       .value = choice_value,
                       .opens = true};
    }
    MenuConfig config = {.title = i18n_text(STR_STANDBY_POWER),
                         .items = screens->power_items,
                         .item_count = 2,
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

static int option_named(SettingsScreens *screens, const SettingChoice *choice, DebugSpan option)
{
    ChoiceConfig config = {.option_ids = choice->options, .option_count = choice->option_count};
    if (choice->options == NULL) {
        choice->extend(screens, &config);
    }
    for (int index = 0; index < config.option_count; index++) {
        const char *text =
            config.option_ids != NULL ? i18n_text(config.option_ids[index]) : config.options[index];
        if (debug_span_matches(option, text)) {
            return index;
        }
    }
    return -1;
}

bool settings_screens_choose(SettingsScreens *screens, DebugSpan title, DebugSpan option)
{
    for (int index = 0; index < SETTINGS_CHOICE_COUNT; index++) {
        const SettingChoice *choice = &CHOICES[index];
        int chosen = debug_span_matches(title, i18n_text(choice->title))
                         ? option_named(screens, choice, option)
                         : -1;
        if (chosen >= 0) {
            choice->choose(&screens->context, chosen);
            return true;
        }
    }
    return button_settings_choose(&screens->buttons, title, option);
}

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
    CardFormatConfig config = {.player = screens->context.player,
                               .theme = screens->theme,
                               .notice =
                                   screens->context.format_prepare != NULL ? app_show_notice : NULL,
                               .prepare = screens->context.format_prepare,
                               .finished = screens->context.format_finished};
    return screen_action_push(card_format_screen_init(&screens->card_format, config));
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
    screens->items[SETTINGS_STATUS_CHOICE + 5] = (MenuItem){.label = i18n_text(STR_STATUS_BAR),
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

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context, const Theme *theme)
{
    screens->context = context;
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
    bind_headphones(screens);
    screens->items[SETTINGS_CONTROLS_ITEM] = (MenuItem){.label = i18n_text(STR_CONTROLS),
                                                        .select = menu_open_screen,
                                                        .context = controls,
                                                        .opens = true};
    screens->items[SETTINGS_DATE_TIME_ITEM] = (MenuItem){.label = i18n_text(STR_SET_CLOCK),
                                                         .select = open_clock,
                                                         .context = screens,
                                                         .value = clock_summary,
                                                         .opens = true};
    screens->items[SETTINGS_RESCAN_ITEM] = (MenuItem){.label = i18n_text(STR_RESCAN_LIBRARY),
                                                      .select = request_rescan,
                                                      .context = &screens->context};
    screens->items[SETTINGS_BLUETOOTH_ITEM] = (MenuItem){.label = i18n_text(STR_BLUETOOTH),
                                                         .select = bluetooth_screen_open,
                                                         .context = context.bluetooth,
                                                         .value = bluetooth_screen_summary,
                                                         .opens = true};
    screens->items[SETTINGS_CUSTOM_EQ_ITEM] = (MenuItem){.label = i18n_text(STR_CUSTOM_EQ),
                                                         .select = menu_open_screen,
                                                         .context = context.graphic,
                                                         .opens = true};
    screens->items[SETTINGS_PARAMETRIC_ITEM] = (MenuItem){.label = i18n_text(STR_PEQ),
                                                          .select = menu_open_screen,
                                                          .context = context.parametric,
                                                          .opens = true};
    for (int index = 0; index < SETTINGS_CHOICE_COUNT; index++) {
        screens->entries[index] = (SettingsEntry){.choice = &CHOICES[index], .owner = screens};
        if (index >= SETTINGS_MAIN_CHOICE_COUNT) {
            continue;
        }
        int item = index + 1;
        item = item < SETTINGS_BLUETOOTH_ITEM ? item : item + 1;
        item = item < SETTINGS_DATE_TIME_ITEM ? item : item + 1;
        item = item < SETTINGS_CUSTOM_EQ_ITEM ? item : item + 1;
        item = item < SETTINGS_RESCAN_ITEM ? item : item + 1;
        screens->items[item] = (MenuItem){.label = i18n_text(CHOICES[index].title),
                                          .select = open_choice,
                                          .context = &screens->entries[index],
                                          .value = choice_value,
                                          .opens = true};
    }
    screens->items[SETTINGS_POWER_ITEM] = (MenuItem){.label = i18n_text(STR_STANDBY_POWER),
                                                     .select = open_power,
                                                     .context = screens,
                                                     .opens = true};
    bind_status_bar(screens);
    screens->items[SETTINGS_LANGUAGE_ITEM].shown = language_shown;
    screens->items[SETTINGS_LANGUAGE_ITEM].shown_context = &screens->languages;
    screens->items[SETTINGS_TRANSITION_LENGTH_ITEM].shown = transition_length_shown;
    screens->items[SETTINGS_TRANSITION_LENGTH_ITEM].shown_context = &screens->context;
    MenuConfig menu = {.title = i18n_text(STR_SETTINGS),
                       .items = screens->items,
                       .item_count = SETTINGS_ITEM_COUNT,
                       .theme = theme,
                       .detail_below = true};
    return menu_screen_init(&screens->menu, menu);
}

ScreenAction settings_screens_sleep(SettingsScreens *screens)
{
    return open_choice(&screens->entries[2]);
}
