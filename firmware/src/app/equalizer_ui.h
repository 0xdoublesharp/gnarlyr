#pragma once

#include "equalizer_presets.h"
#include "screens/equalizer_browser.h"
#include "screens/equalizer_screen.h"
#include "screens/parametric_screen.h"
#include "screens/quick_settings_screen.h"
#include "settings_store.h"
#include "ui/toast.h"

typedef struct EqualizerUi {
    EqualizerBrowser browser;
    EqualizerScreen graphic;
    ParametricScreen parametric;
    EqualizerPresets *presets;
    SettingsStore *store;
    Player *player;
    Toast *toast;
    const Theme *theme;
    char message[64];
    void (*enter_graphic)(void *context, uint32_t now_ms);
    void (*enter_parametric)(void *context, uint32_t now_ms);
    void (*enter_browser)(void *context, uint32_t now_ms);
    QuickSettingsScreen quick_settings;
} EqualizerUi;

void equalizer_ui_init(EqualizerUi *ui, SettingsStore *store, Player *player, Toast *toast,
                       const Theme *theme);
void equalizer_ui_cancel(EqualizerUi *ui);
