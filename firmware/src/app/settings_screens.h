#pragma once

#include <stdint.h>

#include "button_settings.h"
#include "core/power_policy.h"
#include "player.h"
#include "screens/bluetooth_screen.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SETTINGS_CHOICE_COUNT = 11,
    SETTINGS_GROUP_COUNT = 4,
    SETTINGS_GROUP_ITEM_COUNT = 4,
    SETTINGS_ITEM_COUNT = 8
};

typedef struct SettingsContext {
    SettingsStore *store;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    bool *rescan_requested;
    BluetoothScreen *bluetooth;
    Screen *diagnostics;
    int sleep_option;
} SettingsContext;

typedef struct SettingChoice {
    const char *title;
    const char *const *options;
    int option_count;
    int (*current)(const void *context);
    void (*choose)(void *context, int index);
    const char *(*describe)(int current);
} SettingChoice;

typedef struct SettingsScreens SettingsScreens;

typedef struct SettingsEntry {
    const SettingChoice *choice;
    SettingsScreens *owner;
} SettingsEntry;

typedef struct SettingsGroupEntry {
    SettingsScreens *owner;
    unsigned index;
} SettingsGroupEntry;

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    MenuScreen menu;
    MenuScreen group;
    ChoiceScreen choice;
    ButtonSettings buttons;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    MenuItem items[SETTINGS_ITEM_COUNT];
    MenuItem group_items[SETTINGS_GROUP_ITEM_COUNT];
    SettingsGroupEntry groups[SETTINGS_GROUP_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);

ScreenAction settings_screens_open_audio(void *context);
