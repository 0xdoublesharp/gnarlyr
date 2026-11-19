#pragma once

#include "settings_screens.h"
#include "ui/choice_screen.h"
#include "ui/list_view.h"

typedef struct QuickSettingsConfig {
    SettingsContext settings;
    const Theme *theme;
    Screen *lyrics;
    Screen *up_next;
} QuickSettingsConfig;

typedef struct QuickSettingsScreen {
    Screen screen;
    QuickSettingsConfig config;
    ListModel model;
    ListView list;
    ChoiceScreen values;
    uint32_t now_ms;
} QuickSettingsScreen;

Screen *quick_settings_screen_init(QuickSettingsScreen *panel, QuickSettingsConfig config);
