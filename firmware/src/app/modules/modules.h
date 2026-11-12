#pragma once

#include <stdbool.h>

#include "app_screens.h"
#include "core/debug_script.h"
#include "hal/power.h"
#include "headphone_store.h"
#include "library/music_fingerprint.h"
#include "screen_modules.h"

typedef struct PlayingModuleEntry {
    ScreenModuleEntry module;
    void (*charge_observe)(HalBattery battery, uint32_t now_ms);
    void (*charge_draw)(Canvas *canvas, const Theme *theme, HalBattery battery, uint32_t now_ms);
    int (*section)(const BrowseScreen *browse, int index);
    ListSections (*sections)(const BrowseScreen *browse, int index);
} PlayingModuleEntry;

typedef struct ViewsModuleEntry {
    ScreenModuleEntry module;
    void (*cancel)(MixtapeUi *ui);
    void (*step)(MixtapeUi *ui, uint32_t now_ms);
    Screen *(*add)(MixtapeUi *ui, uint16_t track);
} ViewsModuleEntry;

extern const PlayingModuleEntry PLAYING_MODULE_ENTRY;
extern const ViewsModuleEntry VIEWS_MODULE_ENTRY;
extern const ScreenModuleEntry LISTENING_MODULE_ENTRY;

typedef struct SettingsModuleEntry {
    ScreenModuleEntry module;
    ScreenAction (*menu_hold)(AppScreens *screens, uint8_t action);
    Screen *(*language_row)(AppScreens *screens);
    Screen *(*usb_modes)(AppScreens *screens, bool prompt);
    bool (*choose)(AppScreens *screens, DebugSpan title, DebugSpan option);
    void (*dac_headphones)(HeadphoneStore *store, uint8_t chosen, Player *player,
                           SettingsStore *settings);
} SettingsModuleEntry;

extern const SettingsModuleEntry SETTINGS_MODULE_ENTRY;

typedef struct LibraryModuleEntry {
    ScreenModuleEntry module;
    MusicFingerprint (*music_fingerprint)(void);
    Screen *(*open)(AppScreens *screens, BrowseQuery source, bool edit_search);
    Screen *(*options)(AppScreens *screens, BrowseQuery source);
    Screen *(*info)(AppScreens *screens, uint16_t track);
    Screen *(*tags)(AppScreens *screens, uint16_t track);
    Screen *(*browse)(AppScreens *screens, BrowseScreen *browse, bool play_group);
    void (*step)(AppScreens *screens, uint32_t now_ms);
    void (*cancel)(AppScreens *screens);
} LibraryModuleEntry;

extern const LibraryModuleEntry LIBRARY_MODULE_ENTRY;

extern const ParametricModuleEntry EQUALIZER_MODULE_ENTRY;
