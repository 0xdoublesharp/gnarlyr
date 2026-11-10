#pragma once

#include <stdint.h>

#include "core/debug_script.h"
#include "core/power_policy.h"
#include "equalizer_presets.h"
#include "player.h"
#include "screens/bluetooth_screen.h"
#include "screens/equalizer_screen.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SETTINGS_CHOICE_COUNT = 15,
    SETTINGS_ABOUT_ITEM = 0,
    SETTINGS_BLUETOOTH_ITEM = 3,
    SETTINGS_CUSTOM_EQ_ITEM = 15,
    SETTINGS_RESCAN_ITEM = 17,
    SETTINGS_ITEM_COUNT = SETTINGS_CHOICE_COUNT + 4
};

typedef struct SettingsContext {
    SettingsStore *store;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    bool *rescan_requested;
    BluetoothScreen *bluetooth;
    Theme *theme;
    EqualizerPresets *presets;
    int sleep_option;
} SettingsContext;

typedef struct SettingsScreens SettingsScreens;

typedef struct SettingChoice {
    const char *title;
    const char *const *options;
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
    EqualizerPresets presets;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    MenuItem items[SETTINGS_ITEM_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
bool settings_screens_choose(SettingsScreens *screens, DebugSpan title, DebugSpan option);
