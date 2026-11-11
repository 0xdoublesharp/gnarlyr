#pragma once

#include "equalizer_presets.h"
#include "settings/equalizer_response.h"
#include "ui/choice_screen.h"

typedef struct EqualizerBrowserConfig {
    ChoiceConfig choice;
    const Settings *settings;
    EqualizerPresets *presets;
    void (*preview)(void *context, const SettingsEqualizerCurve *curve);
} EqualizerBrowserConfig;

typedef struct EqualizerBrowser {
    ChoiceScreen choice;
    EqualizerBrowserConfig config;
    EqualizerResponse *response;
    SettingsEqualizerCurve committed;
    ScreenAction (*choose_key)(void *context, KeyEvent event, uint32_t now_ms);
    void (*choice_enter)(void *context, uint32_t now_ms);
    int preview_index;
    bool valid;
    bool previewing;
} EqualizerBrowser;

void equalizer_browser_leave(EqualizerBrowser *browser);
Screen *equalizer_browser_init(EqualizerBrowser *browser, EqualizerBrowserConfig config);
