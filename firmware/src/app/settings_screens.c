#include "settings_screens.h"

#include <stddef.h>

#include "default_theme.h"
#include "hal/audio.h"
#include "hal/clock.h"

enum {
    BRIGHTNESS_LEVEL_COUNT = SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN + 1,
    MILLISECONDS_PER_MINUTE = 60000
};

static const char *const SHUFFLE_OPTIONS[SHUFFLE_MODE_COUNT] = {"Off", "Songs", "Albums"};
static const char *const REPEAT_OPTIONS[REPEAT_MODE_COUNT] = {"Off", "One", "All"};
static const char *const SLEEP_OPTIONS[SLEEP_TIMER_OPTION_COUNT] = {
    "Off", "15 min", "30 min", "45 min", "60 min", "90 min", "120 min"};
static const char *const SLEEP_MESSAGES[SLEEP_TIMER_OPTION_COUNT] = {
    "Sleep timer off",       "Turning off in 15 min", "Turning off in 30 min",
    "Turning off in 45 min", "Turning off in 60 min", "Turning off in 90 min",
    "Turning off in 120 min"};
static const char *const AUTO_OFF_OPTIONS[AUTO_OFF_OPTION_COUNT] = {"Off", "1 min", "5 min",
                                                                    "10 min"};
static const char *const TIMEOUT_OPTIONS[SCREEN_TIMEOUT_OPTION_COUNT] = {
    "10 s", "20 s", "30 s", "1 min", "2 min", "5 min", "Never"};
static const char *const BRIGHTNESS_OPTIONS[BRIGHTNESS_LEVEL_COUNT] = {"1", "2", "3", "4", "5",
                                                                       "6", "7", "8", "9", "10"};
static const char *const ACCENT_OPTIONS[ACCENT_COLOR_COUNT] = {
    "Blue", "Red", "Orange", "Green", "Teal", "Purple", "Pink", "Graphite"};
static const char *const TEXT_SIZE_OPTIONS[TEXT_SIZE_COUNT] = {"Small", "Medium", "Large"};
static const char *const ORIENTATION_OPTIONS[ORIENTATION_COUNT] = {"Landscape", "Portrait",
                                                                   "Portrait, flipped"};
static const char *const SKIP_OPTIONS[SKIP_GESTURE_COUNT] = {
    "Hold vol\xC2\xB1", "Double vol\xC2\xB1", "Double/triple Play"};
static const char *const GAIN_OPTIONS[GAIN_LEVEL_COUNT] = {"Low", "High"};
static const char *const OUTPUT_OPTIONS[OUTPUT_JACK_COUNT] = {"Auto", "3.5 mm", "4.4 mm"};
static const char *const OUTPUT_MESSAGES[OUTPUT_JACK_COUNT] = {
    "Volume follows the plugged jack", "Using 3.5 mm volume levels", "Using 4.4 mm volume levels"};
static const char *const START_OPTIONS[START_SCREEN_COUNT] = {
    "Main menu", "Now Playing", "Albums", "Artists", "Favorites", "Playlists", "Cover Flow"};
static const char *const USB_MODE_OPTIONS[USB_MODE_COUNT] = {"Storage", "DAC"};
static const char *const EQUALIZER_OPTIONS[EQUALIZER_PRESET_COUNT] = {
    "Off", "Retro", "Bass", "Heavy", "Pop", "Jazz", "Unique", "Custom"};
static const char *const SLOT_MESSAGES[EQUALIZER_SLOT_COUNT] = {
    "Saved to slot 1", "Saved to slot 2", "Saved to slot 3", "Saved to slot 4",
    "Saved to slot 5", "Saved to slot 6", "Saved to slot 7", "Saved to slot 8"};
static const char *const USB_MODE_MESSAGES[USB_MODE_COUNT] = {"USB shows the player's drives",
                                                              "USB plays the computer's sound"};

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
        toast_show(context->toast, "Could not save settings", hal_clock_milliseconds());
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
    toast_show(settings->toast, SLEEP_MESSAGES[index], now_ms);
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

static int current_accent(const void *context)
{
    return (int)settings_of(context)->accent;
}

static void choose_accent(void *context, int index)
{
    editable_settings(context)->accent = (AccentColor)index;
    save(context);
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
    toast_show(settings->toast, OUTPUT_MESSAGES[index], hal_clock_milliseconds());
}

static const char *describe_output(const void *context, int current)
{
    (void)context;
    HalAudioJack jack = hal_audio_jack();
    if (current != OUTPUT_AUTO || jack == HAL_AUDIO_JACK_NONE) {
        return OUTPUT_OPTIONS[current];
    }
    return jack == HAL_AUDIO_JACK_BALANCED ? "Auto (4.4 mm)" : "Auto (3.5 mm)";
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
    return slot < 0 ? EQUALIZER_OPTIONS[current] : EQUALIZER_SLOT_LABELS[slot];
}

static void choose_equalizer(void *context, int index)
{
    SettingsContext *settings = context;
    Settings *chosen = editable_settings(context);
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    if (index >= EQUALIZER_PRESET_COUNT) {
        if (!equalizer_presets_load(settings->presets, index, gains)) {
            toast_show(settings->toast, "Could not read the EQ file", hal_clock_milliseconds());
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
    toast_show(settings->toast, saved ? SLOT_MESSAGES[slot] : "Could not save the slot",
               hal_clock_milliseconds());
}

static Screen *equalizer_editor(SettingsScreens *screens, int slot)
{
    EqualizerScreenConfig config = {.settings = &screens->context.store->settings,
                                    .changed = equalizer_changed,
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
    equalizer_presets_scan(&screens->presets, EQUALIZER_OPTIONS);
    config->options = screens->presets.options;
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
    return (int)settings_of(context)->usb_mode;
}

static void choose_usb_mode(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->usb_mode = (UsbMode)index;
    apply_and_save(context);
    toast_show(settings->toast, USB_MODE_MESSAGES[index], hal_clock_milliseconds());
}

static const SettingChoice CHOICES[SETTINGS_CHOICE_COUNT] = {
    {"Shuffle", SHUFFLE_OPTIONS, SHUFFLE_MODE_COUNT, current_shuffle, choose_shuffle, NULL, NULL},
    {"Repeat", REPEAT_OPTIONS, REPEAT_MODE_COUNT, current_repeat, choose_repeat, NULL, NULL},
    {"Sleep timer", SLEEP_OPTIONS, SLEEP_TIMER_OPTION_COUNT, current_sleep, choose_sleep, NULL,
     NULL},
    {"Auto off", AUTO_OFF_OPTIONS, AUTO_OFF_OPTION_COUNT, current_auto_off, choose_auto_off, NULL,
     NULL},
    {"Screen timeout", TIMEOUT_OPTIONS, SCREEN_TIMEOUT_OPTION_COUNT, current_timeout,
     choose_timeout, NULL, NULL},
    {"Brightness", BRIGHTNESS_OPTIONS, BRIGHTNESS_LEVEL_COUNT, current_brightness,
     choose_brightness, NULL, NULL},
    {"Color", ACCENT_OPTIONS, ACCENT_COLOR_COUNT, current_accent, choose_accent, NULL, NULL},
    {"Text size", TEXT_SIZE_OPTIONS, TEXT_SIZE_COUNT, current_text_size, choose_text_size, NULL,
     NULL},
    {"Orientation", ORIENTATION_OPTIONS, ORIENTATION_COUNT, current_orientation, choose_orientation,
     NULL, NULL},
    {"Skip songs", SKIP_OPTIONS, SKIP_GESTURE_COUNT, current_skip, choose_skip, NULL, NULL},
    {"Gain", GAIN_OPTIONS, GAIN_LEVEL_COUNT, current_gain, choose_gain, NULL, NULL},
    {"Output", OUTPUT_OPTIONS, OUTPUT_JACK_COUNT, current_output, choose_output, describe_output,
     NULL},
    {"Equalizer", EQUALIZER_OPTIONS, EQUALIZER_PRESET_COUNT, current_equalizer, choose_equalizer,
     describe_equalizer, extend_equalizer},
    {"Start screen", START_OPTIONS, START_SCREEN_COUNT, current_start, choose_start, NULL, NULL},
    {"USB mode", USB_MODE_OPTIONS, USB_MODE_COUNT, current_usb_mode, choose_usb_mode, NULL, NULL},
};

static ScreenAction open_choice(void *context)
{
    SettingsEntry *entry = context;
    SettingsScreens *screens = entry->owner;
    const SettingChoice *choice = entry->choice;
    ChoiceConfig config = {
        .title = choice->title,
        .options = choice->options,
        .option_count = choice->option_count,
        .current = choice->current,
        .choose = choice->choose,
        .context = &screens->context,
        .theme = screens->theme,
    };
    if (choice->extend != NULL) {
        choice->extend(screens, &config);
    }
    return screen_action_push(choice_screen_init(&screens->choice, config));
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
    return entry->choice->options[current];
}

static ScreenAction request_rescan(void *context)
{
    SettingsContext *settings = context;
    if (settings->rescan_requested != NULL) {
        *settings->rescan_requested = true;
    }
    return screen_action_none();
}

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context, const Theme *theme)
{
    screens->context = context;
    screens->context.presets = &screens->presets;
    screens->theme = theme;
    equalizer_presets_read_slots(&screens->presets);
    screens->items[SETTINGS_RESCAN_ITEM] = (MenuItem){
        .label = "Rescan library", .select = request_rescan, .context = &screens->context};
    screens->items[SETTINGS_BLUETOOTH_ITEM] = (MenuItem){.label = "Bluetooth",
                                                         .select = bluetooth_screen_open,
                                                         .context = context.bluetooth,
                                                         .value = bluetooth_screen_summary,
                                                         .opens = true};
    screens->items[SETTINGS_CUSTOM_EQ_ITEM] = (MenuItem){
        .label = "Custom EQ", .select = open_custom_equalizer, .context = screens, .opens = true};
    for (int index = 0; index < SETTINGS_CHOICE_COUNT; index++) {
        screens->entries[index] = (SettingsEntry){.choice = &CHOICES[index], .owner = screens};
        int item = index + 1;
        item = item < SETTINGS_BLUETOOTH_ITEM ? item : item + 1;
        item = item < SETTINGS_CUSTOM_EQ_ITEM ? item : item + 1;
        item = item < SETTINGS_RESCAN_ITEM ? item : item + 1;
        screens->items[item] = (MenuItem){.label = CHOICES[index].title,
                                          .select = open_choice,
                                          .context = &screens->entries[index],
                                          .value = choice_value,
                                          .opens = true};
    }
    MenuConfig menu = {.title = "Settings",
                       .items = screens->items,
                       .item_count = SETTINGS_ITEM_COUNT,
                       .theme = theme};
    return menu_screen_init(&screens->menu, menu);
}
