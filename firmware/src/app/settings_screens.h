#pragma once

#include <stdint.h>

#include "button_settings.h"
#include "core/debug_script.h"
#include "core/power_policy.h"
#include "equalizer_presets.h"
#include "headphone_editor.h"
#include "language.h"
#include "player.h"
#include "screens/bluetooth_screen.h"
#include "screens/card_format_screen.h"
#include "screens/equalizer_browser.h"
#include "screens/equalizer_screen.h"
#include "screens/set_clock_screen.h"
#include "settings_preview.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SETTINGS_CHOICE_COUNT = 32,
    SETTINGS_MAIN_CHOICE_COUNT = 28,
    SETTINGS_STATUS_CHOICE = 21,
    SETTINGS_STATUS_COUNT = 5,
    SETTINGS_LANGUAGE_CHOICE = 10,
    SETTINGS_USB_MODE_CHOICE = 19,
    SETTINGS_ABOUT_ITEM = 0,
    SETTINGS_BLUETOOTH_ITEM = 3,
    SETTINGS_DATE_TIME_ITEM = 8,
    SETTINGS_LANGUAGE_ITEM = 13,
    SETTINGS_CUSTOM_EQ_ITEM = 19,
    SETTINGS_RESCAN_ITEM = 23,
    SETTINGS_TRANSITION_LENGTH_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 2,
    SETTINGS_HEADPHONES_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 5,
    SETTINGS_FORMAT_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 8,
    SETTINGS_HOME_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 9,
    SETTINGS_CONTROLS_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 10,
    SETTINGS_EJECT_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 6,
    SETTINGS_MOUNT_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 7,
    SETTINGS_ITEM_COUNT = SETTINGS_MAIN_CHOICE_COUNT + 11,
    SETTINGS_MESSAGE_CAPACITY = 64,
    SETTINGS_COUNTDOWN_CAPACITY = 16
};

enum {
    SETTINGS_EQ_PREVIEW_NONE,
    SETTINGS_EQ_PREVIEW_BROWSER,
    SETTINGS_EQ_PREVIEW_EDITOR
};

typedef struct SettingsContext {
    SettingsStore *store;
    HeadphoneStore *headphones;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    void (*format_prepare)(void);
    void (*format_finished)(bool success);
    bool *rescan_requested;
    bool *language_requested;
    BluetoothScreen *bluetooth;
    Theme *theme;
    EqualizerPresets *presets;
    const LanguageList *languages;
    char message[SETTINGS_MESSAGE_CAPACITY];
} SettingsContext;

typedef struct SettingsScreens SettingsScreens;

typedef struct SettingChoice {
    StringId title;
    const StringId *options;
    int option_count;
    int (*current)(const void *context);
    void (*choose)(void *context, int index);
    const char *(*describe)(const void *context, int current);
    void (*extend)(SettingsScreens *screens, ChoiceConfig *config);
} SettingChoice;

typedef struct SettingsEntry {
    const SettingChoice *choice;
    SettingsScreens *owner;
} SettingsEntry;

typedef struct UsbChooser {
    Screen screen;
    ChoiceScreen choice;
    uint32_t until_ms;
    uint32_t seconds;
    bool timed;
    bool expired;
    bool redraw;
    char countdown[SETTINGS_COUNTDOWN_CAPACITY];
} UsbChooser;

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    StringId choice_preview;
    SettingsPreview preview;
    const Settings *(*preview_settings)(SettingsScreens *screens, bool active);
    uint8_t equalizer_preview;
    MenuScreen menu;
    MenuScreen status_menu;
    MenuItem status_items[SETTINGS_STATUS_COUNT];
    union {
        ChoiceScreen choice;
        SetClockScreen clock;
        EqualizerBrowser equalizer_browser;
        EqualizerScreen equalizer;
    };
    ButtonSettings buttons;
    HeadphoneEditor headphones;
    LanguageList languages;
    UsbChooser usb;
    CardFormatScreen card_format;
    bool card_needs_format;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    MenuItem items[SETTINGS_ITEM_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
bool settings_screens_choose(SettingsScreens *screens, DebugSpan title, DebugSpan option);
Screen *settings_screens_usb_modes(SettingsScreens *screens, bool prompt);

ScreenAction settings_screens_sleep(SettingsScreens *screens);
