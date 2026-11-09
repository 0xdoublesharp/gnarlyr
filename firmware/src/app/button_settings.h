#pragma once

#include "settings/settings.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"

typedef struct ButtonSettingsConfig {
    Settings *settings;
    const Theme *theme;
    void (*changed)(void *context);
    void *context;
} ButtonSettingsConfig;

typedef struct ButtonSettings ButtonSettings;

typedef struct ButtonEntry {
    ButtonSettings *owner;
    PlaybackButton button;
} ButtonEntry;

struct ButtonSettings {
    ButtonSettingsConfig config;
    MenuScreen menu;
    ChoiceScreen choice;
    MenuItem items[PLAYBACK_BUTTON_COUNT + 1];
    ButtonEntry entries[PLAYBACK_BUTTON_COUNT];
};

Screen *button_settings_init(ButtonSettings *buttons, ButtonSettingsConfig config);
