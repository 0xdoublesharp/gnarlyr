#include "settings_screens.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "default_theme.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/system.h"

enum {
    BRIGHTNESS_LEVEL_COUNT = SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN + 1,
    MILLISECONDS_PER_MINUTE = 60000,
    USB_MODE_SELECT_COUNT = 3
};

static const StringId SHUFFLE_OPTIONS[SHUFFLE_MODE_COUNT] = {STR_OFF, STR_SHUFFLE_SONGS,
                                                             STR_SHUFFLE_ALBUMS};
static const StringId REPEAT_OPTIONS[REPEAT_MODE_COUNT] = {STR_OFF, STR_REPEAT_MODE_ONE,
                                                           STR_REPEAT_MODE_ALL};
static const StringId SLEEP_OPTIONS[SLEEP_TIMER_OPTION_COUNT] = {
    STR_OFF,        STR_MINUTES_15, STR_MINUTES_30, STR_MINUTES_45,
    STR_MINUTES_60, STR_MINUTES_90, STR_MINUTES_120};
static const StringId AUTO_OFF_OPTIONS[AUTO_OFF_OPTION_COUNT] = {STR_OFF, STR_MINUTES_1,
                                                                 STR_MINUTES_5, STR_MINUTES_10};
static const StringId TIMEOUT_OPTIONS[SCREEN_TIMEOUT_OPTION_COUNT] = {
    STR_SECONDS_10, STR_SECONDS_20, STR_SECONDS_30, STR_MINUTES_1,
    STR_MINUTES_2,  STR_MINUTES_5,  STR_NEVER};
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
static const StringId OUTPUT_OPTIONS[OUTPUT_JACK_COUNT] = {STR_OUTPUT_AUTO, STR_JACK_35,
                                                           STR_JACK_44};
static const StringId OUTPUT_MESSAGES[OUTPUT_JACK_COUNT] = {STR_OUTPUT_FOLLOWS, STR_OUTPUT_USING_35,
                                                            STR_OUTPUT_USING_44};
static const StringId START_OPTIONS[START_SCREEN_COUNT] = {
    STR_START_MAIN_MENU, STR_NOW_PLAYING, STR_ALBUMS,    STR_ARTISTS,
    STR_FAVORITES,       STR_PLAYLISTS,   STR_COVER_FLOW};
static const StringId USB_MODE_OPTIONS[USB_MODE_SELECT_COUNT] = {STR_PLAYER_MODE, STR_POWER_STORAGE,
                                                                 STR_USB_DAC_TITLE};
static const StringId USB_MODE_MESSAGES[USB_MODE_COUNT] = {STR_USB_SHOWS_DRIVES,
                                                           STR_USB_PLAYS_SOUND};

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

static void save(SettingsContext *context)
{
    settings_store_capture(context->store, player_status(context->player));
    if (!settings_store_save(context->store)) {
        toast_show(context->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
}

static void apply_and_save(SettingsContext *context)
{
    settings_store_apply_hardware(&context->store->settings);
    save(context);
}

static int current_shuffle(const void *context)
{
    const SettingsContext *settings = context;
    return (int)player_status(settings->player).shuffle;
}

static void choose_shuffle(void *context, int index)
{
    SettingsContext *settings = context;
    player_set_shuffle(settings->player, (ShuffleMode)index, hal_clock_microseconds());
    save(settings);
}

static int current_repeat(const void *context)
{
    const SettingsContext *settings = context;
    return (int)player_status(settings->player).repeat;
}

static void choose_repeat(void *context, int index)
{
    SettingsContext *settings = context;
    player_set_repeat(settings->player, (RepeatMode)index);
    save(settings);
}

static int current_sleep(const void *context)
{
    const SettingsContext *settings = context;
    return settings->power->sleep_timer_active ? settings->sleep_option : 0;
}

static void choose_sleep(void *context, int index)
{
    SettingsContext *settings = context;
    uint32_t now_ms = hal_clock_milliseconds();
    uint32_t minutes = settings_sleep_option_minutes(index);
    settings->sleep_option = index;
    if (minutes == 0) {
        power_policy_cancel_sleep_timer(settings->power);
    } else {
        power_policy_start_sleep_timer(settings->power, minutes * MILLISECONDS_PER_MINUTE, now_ms);
    }
    StringBuilder message = string_builder_make(settings->message, sizeof settings->message);
    if (minutes == 0) {
        string_builder_append(&message, i18n_text(STR_SLEEP_OFF));
    } else {
        i18n_format_text(&message, STR_SLEEP_IN, i18n_text(SLEEP_OPTIONS[index]));
    }
    toast_show(settings->toast, settings->message, now_ms);
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
    save(settings);
}

static int current_auto_off(const void *context)
{
    return settings_of(context)->auto_off;
}

static void choose_auto_off(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->auto_off = (uint8_t)index;
    power_policy_set_auto_off(settings->power, settings_auto_off_ms(settings_of(context)),
                              hal_clock_milliseconds());
    save(settings);
}

static int current_brightness(const void *context)
{
    return settings_of(context)->brightness - SETTINGS_BRIGHTNESS_MIN;
}

static void choose_brightness(void *context, int index)
{
    editable_settings(context)->brightness = (uint8_t)(index + SETTINGS_BRIGHTNESS_MIN);
    apply_and_save(context);
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
    save(context);
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
    save(settings);
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
    save(settings);
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
    save(settings);
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
    save(settings);
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
    save(context);
}

static int current_gain(const void *context)
{
    return (int)settings_of(context)->gain;
}

static void choose_gain(void *context, int index)
{
    editable_settings(context)->gain = (GainLevel)index;
    apply_and_save(context);
}

static int current_output(const void *context)
{
    return (int)settings_of(context)->output;
}

static void choose_output(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->output = (OutputJack)index;
    apply_and_save(context);
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

static int active_slot(const void *context)
{
    const SettingsContext *settings = context;
    const Settings *chosen = settings_of(context);
    return chosen->equalizer == EQUALIZER_CUSTOM
               ? equalizer_presets_matching_slot(settings->presets, chosen->equalizer_gains)
               : -1;
}

static int current_equalizer(const void *context)
{
    const SettingsContext *settings = context;
    int slot = active_slot(context);
    return slot < 0 ? (int)settings_of(context)->equalizer
                    : equalizer_presets_slot_option(settings->presets, slot);
}

static const char *describe_equalizer(const void *context, int current)
{
    int slot = active_slot(context);
    return slot < 0 ? i18n_text(EQUALIZER_PRESET_NAMES[current])
                    : equalizer_presets_slot_label(slot);
}

static void choose_equalizer(void *context, int index)
{
    SettingsContext *settings = context;
    Settings *chosen = editable_settings(context);
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    if (index >= EQUALIZER_PRESET_COUNT) {
        if (!equalizer_presets_load(settings->presets, index, gains)) {
            toast_show(settings->toast, i18n_text(STR_EQ_FILE_UNREADABLE),
                       hal_clock_milliseconds());
            return;
        }
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            chosen->equalizer_gains[band] = gains[band];
        }
        index = EQUALIZER_CUSTOM;
    }
    chosen->equalizer = (EqualizerPreset)index;
    apply_and_save(context);
}

static void equalizer_changed(void *context, bool finished)
{
    SettingsContext *settings = context;
    settings_store_apply_hardware(&settings->store->settings);
    if (finished) {
        save(settings);
    }
}

static bool slot_used(const void *context, int slot)
{
    const SettingsContext *settings = context;
    return equalizer_slots_used(&settings->presets->slots, slot);
}

static void save_slot(void *context, int slot)
{
    SettingsContext *settings = context;
    bool saved = equalizer_presets_save_slot(settings->presets, slot,
                                             settings->store->settings.equalizer_gains);
    StringBuilder message = string_builder_make(settings->message, sizeof settings->message);
    if (saved) {
        i18n_format_counts(&message, STR_EQ_SAVED, (uint32_t)slot + 1, 0);
    } else {
        string_builder_append(&message, i18n_text(STR_EQ_SLOT_NOT_SAVED));
    }
    toast_show(settings->toast, settings->message, hal_clock_milliseconds());
}

static void preview_equalizer(void *context, const SettingsEqualizerCurve *curve)
{
    (void)context;
    HalAudioEqualizer equalizer = {.band_count = curve->band_count, .pre_gain = curve->pre_gain};
    for (int band = 0; band < curve->band_count; band++) {
        equalizer.band_hz[band] = curve->band_hz[band];
        equalizer.gain_half_db[band] = curve->gain_half_db[band];
    }
    hal_audio_set_equalizer(&equalizer);
}

static Screen *equalizer_editor(SettingsScreens *screens, int slot)
{
    screens->appearance_preview = false;
    screens->equalizer_preview = SETTINGS_EQ_PREVIEW_EDITOR;
    EqualizerScreenConfig config = {.settings = &screens->context.store->settings,
                                    .changed = equalizer_changed,
                                    .preview = preview_equalizer,
                                    .slot_used = slot_used,
                                    .save_slot = save_slot,
                                    .context = &screens->context,
                                    .theme = screens->theme,
                                    .slot = slot < 0 ? 0 : slot};
    return equalizer_screen_init(&screens->equalizer, config);
}

static ScreenAction open_custom_equalizer(void *context)
{
    SettingsScreens *screens = context;
    equalizer_presets_read_slots(&screens->presets);
    return screen_action_push(equalizer_editor(screens, active_slot(&screens->context)));
}

static Screen *open_chosen_equalizer(void *context, int index)
{
    SettingsScreens *screens =
        (SettingsScreens *)((char *)context - offsetof(SettingsScreens, context));
    int slot = equalizer_presets_option_slot(&screens->presets, index);
    bool editable = slot >= 0 || index == EQUALIZER_CUSTOM;
    return editable && screens->context.store->settings.equalizer == EQUALIZER_CUSTOM
               ? equalizer_editor(screens, slot)
               : NULL;
}

static void extend_equalizer(SettingsScreens *screens, ChoiceConfig *config)
{
    equalizer_presets_scan(&screens->presets);
    config->options = screens->presets.options;
    config->option_ids = NULL;
    config->option_count = screens->presets.option_count;
    config->open = open_chosen_equalizer;
}

static int current_start(const void *context)
{
    return (int)settings_of(context)->start_screen;
}

static void choose_start(void *context, int index)
{
    editable_settings(context)->start_screen = (StartScreen)index;
    save(context);
}

static int current_usb_mode(const void *context)
{
    (void)context;
    return 0;
}

static void choose_usb_mode(void *context, int index)
{
    if (index == 0) {
        hal_system_request_usb_mode(HAL_USB_MODE_PLAYER);
        return;
    }
    SettingsContext *settings = context;
    editable_settings(context)->usb_mode = (UsbMode)(index - 1);
    apply_and_save(context);
    HalUsbMode mode = index == 1 ? HAL_USB_MODE_STORAGE : HAL_USB_MODE_DAC;
    hal_system_request_usb_mode(mode);
    toast_show(settings->toast, i18n_text(USB_MODE_MESSAGES[index - 1]), hal_clock_milliseconds());
}

static const SettingChoice CHOICES[SETTINGS_CHOICE_COUNT] = {
    {STR_SHUFFLE, SHUFFLE_OPTIONS, SHUFFLE_MODE_COUNT, current_shuffle, choose_shuffle, NULL, NULL},
    {STR_REPEAT, REPEAT_OPTIONS, REPEAT_MODE_COUNT, current_repeat, choose_repeat, NULL, NULL},
    {STR_SLEEP_TIMER, SLEEP_OPTIONS, SLEEP_TIMER_OPTION_COUNT, current_sleep, choose_sleep, NULL,
     NULL},
    {STR_AUTO_OFF, AUTO_OFF_OPTIONS, AUTO_OFF_OPTION_COUNT, current_auto_off, choose_auto_off, NULL,
     NULL},
    {STR_SCREEN_TIMEOUT, TIMEOUT_OPTIONS, SCREEN_TIMEOUT_OPTION_COUNT, current_timeout,
     choose_timeout, NULL, NULL},
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
    {STR_OUTPUT, OUTPUT_OPTIONS, OUTPUT_JACK_COUNT, current_output, choose_output, describe_output,
     NULL},
    {STR_EQUALIZER, EQUALIZER_PRESET_NAMES, EQUALIZER_PRESET_COUNT, current_equalizer,
     choose_equalizer, describe_equalizer, extend_equalizer},
    {STR_START_SCREEN, START_OPTIONS, START_SCREEN_COUNT, current_start, choose_start, NULL, NULL},
    {STR_USB_MODE, USB_MODE_OPTIONS, USB_MODE_SELECT_COUNT, current_usb_mode, choose_usb_mode, NULL,
     NULL},
    {STR_THEME, THEME_OPTIONS, APPEARANCE_THEME_COUNT, current_appearance, choose_appearance, NULL,
     NULL},
};

static Screen *choice_view(SettingsScreens *screens, const SettingChoice *choice,
                           ChoiceConfig config)
{
    if (choice->title != STR_EQUALIZER) {
        return choice_screen_init(&screens->choice, config);
    }
    EqualizerBrowserConfig browser = {.choice = config,
                                      .settings = &screens->context.store->settings,
                                      .presets = &screens->presets,
                                      .preview = preview_equalizer};
    return equalizer_browser_init(&screens->equalizer_browser, browser);
}

static ScreenAction open_choice(void *context)
{
    SettingsEntry *entry = context;
    SettingsScreens *screens = entry->owner;
    const SettingChoice *choice = entry->choice;
    screens->appearance_preview = choice->choose == choose_appearance;
    screens->equalizer_preview =
        choice->title == STR_EQUALIZER ? SETTINGS_EQ_PREVIEW_BROWSER : SETTINGS_EQ_PREVIEW_NONE;
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

Screen *settings_screens_usb_modes(SettingsScreens *screens)
{
    return open_choice(&screens->entries[SETTINGS_USB_MODE_CHOICE]).target;
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

static void buttons_changed(void *context)
{
    save(context);
}

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context, const Theme *theme)
{
    screens->context = context;
    screens->context.presets = &screens->presets;
    screens->context.languages = &screens->languages;
    screens->theme = theme;
    screens->appearance_preview = false;
    screens->equalizer_preview = SETTINGS_EQ_PREVIEW_NONE;
    equalizer_presets_read_slots(&screens->presets);
    language_list(&screens->languages);
    ButtonSettingsConfig buttons = {.settings = &screens->context.store->settings,
                                    .theme = theme,
                                    .changed = buttons_changed,
                                    .context = &screens->context};
    Screen *controls = button_settings_init(&screens->buttons, buttons);
    screens->items[SETTINGS_CONTROLS_ITEM] = (MenuItem){.label = i18n_text(STR_CONTROLS),
                                                        .select = menu_open_screen,
                                                        .context = controls,
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
                                                         .select = open_custom_equalizer,
                                                         .context = screens,
                                                         .opens = true};
    for (int index = 0; index < SETTINGS_CHOICE_COUNT; index++) {
        screens->entries[index] = (SettingsEntry){.choice = &CHOICES[index], .owner = screens};
        int item = index + 1;
        item = item < SETTINGS_BLUETOOTH_ITEM ? item : item + 1;
        item = item < SETTINGS_CUSTOM_EQ_ITEM ? item : item + 1;
        item = item < SETTINGS_RESCAN_ITEM ? item : item + 1;
        screens->items[item] = (MenuItem){.label = i18n_text(CHOICES[index].title),
                                          .select = open_choice,
                                          .context = &screens->entries[index],
                                          .value = choice_value,
                                          .opens = true};
    }
    screens->items[SETTINGS_LANGUAGE_ITEM].shown = language_shown;
    screens->items[SETTINGS_LANGUAGE_ITEM].shown_context = &screens->languages;
    MenuConfig menu = {.title = i18n_text(STR_SETTINGS),
                       .items = screens->items,
                       .item_count = SETTINGS_ITEM_COUNT,
                       .theme = theme,
                       .detail_below = true};
    return menu_screen_init(&screens->menu, menu);
}
