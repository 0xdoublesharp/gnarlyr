#pragma once

#include "settings_screens.h"
#include "ui/list_view.h"

typedef struct QuickSettingsSpeed {
    void *context;
    bool (*available)(const void *context);
    const char *(*value)(const void *context);
    void (*cycle)(void *context);
} QuickSettingsSpeed;

typedef struct QuickSettingsConfig {
    SettingsContext settings;
    const Theme *theme;
    Screen *lyrics;
    Screen *up_next;
    QuickSettingsSpeed speed;
} QuickSettingsConfig;

typedef struct QuickSettingsScreen {
    Screen screen;
    QuickSettingsConfig config;
    EqualizerPresets presets;
    ListModel model;
    ListView list;
    uint32_t now_ms;
} QuickSettingsScreen;

Screen *quick_settings_screen_init(QuickSettingsScreen *panel, QuickSettingsConfig config);
