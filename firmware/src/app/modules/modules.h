#pragma once

#include <stdbool.h>

#include "app_screens.h"
#include "core/debug_script.h"
#include "library/music_fingerprint.h"
#include "screen_modules.h"

extern const ScreenModuleEntry VIEWS_MODULE_ENTRY;

typedef struct SettingsModuleEntry {
    ScreenModuleEntry module;
    Screen *(*language_row)(AppScreens *screens);
    bool (*choose)(AppScreens *screens, DebugSpan title, DebugSpan option);
} SettingsModuleEntry;

extern const SettingsModuleEntry SETTINGS_MODULE_ENTRY;

typedef struct LibraryModuleEntry {
    ScreenModuleEntry module;
    MusicFingerprint (*music_fingerprint)(void);
} LibraryModuleEntry;

extern const LibraryModuleEntry LIBRARY_MODULE_ENTRY;
