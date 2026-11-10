#pragma once

#include <stdint.h>

#include "button_settings.h"
#include "core/debug_script.h"
#include "core/power_policy.h"
#include "equalizer_presets.h"
#include "language.h"
#include "player.h"
#include "screens/bluetooth_screen.h"
#include "screens/equalizer_screen.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SETTINGS_CHOICE_COUNT = 17,
    SETTINGS_LANGUAGE_CHOICE = 9,
    SETTINGS_ABOUT_ITEM = 0,
    SETTINGS_BLUETOOTH_ITEM = 3,
    SETTINGS_LANGUAGE_ITEM = 11,
    SETTINGS_CUSTOM_EQ_ITEM = 16,
    SETTINGS_RESCAN_ITEM = 18,
    SETTINGS_CONTROLS_ITEM = 21,
    SETTINGS_ITEM_COUNT = SETTINGS_CHOICE_COUNT + 5,
    SETTINGS_MESSAGE_CAPACITY = 64
};

typedef struct SettingsContext {
    SettingsStore *store;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    bool *rescan_requested;
    bool *language_requested;
    BluetoothScreen *bluetooth;
    Theme *theme;
    EqualizerPresets *presets;
    const LanguageList *languages;
    int sleep_option;
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

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    MenuScreen menu;
    union {
        ChoiceScreen choice;
        EqualizerScreen equalizer;
    };
    ButtonSettings buttons;
    EqualizerPresets presets;
    LanguageList languages;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    MenuItem items[SETTINGS_ITEM_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
bool settings_screens_choose(SettingsScreens *screens, DebugSpan title, DebugSpan option);
