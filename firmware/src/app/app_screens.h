#pragma once

#include <stdbool.h>

#include "browse_screen.h"
#include "core/i18n.h"
#include "core/power_policy.h"
#include "cover_cache.h"
#include "debug_loop_internal.h"
#include "diagnostics_values.h"
#include "favorites_store.h"
#include "hal/audio.h"
#include "home_art.h"
#include "library_store.h"
#include "player.h"
#include "screen_modules.h"
#include "screens/bluetooth_screen.h"
#include "screens/cover_flow_screen.h"
#include "screens/now_playing_screen.h"
#include "screens/up_next_screen.h"
#include "settings/settings.h"
#include "settings_screens.h"
#include "settings_store.h"
#include "ui/menu_screen.h"
#include "ui/message_screen.h"
#include "ui/toast.h"

enum {
    HOME_ITEM_COUNT = 6,
    SHELF_COUNT = 2,
    MUSIC_ITEM_COUNT = 9,
    DIAGNOSTICS_ITEM_COUNT = 15 + DEBUG_LOOP_ABOUT_ROWS,
    START_PATH_CAPACITY = 2
};

typedef struct HighRamOption {
    bool run;
    HalHighRamTest test;
} HighRamOption;

typedef struct AppOptions {
    bool show_frame_time;
    bool ignore_power_taps;
} AppOptions;

typedef struct ClockOption {
    bool failed;
} ClockOption;

typedef struct ToneOption {
    bool tried;
    HalToneResult result;
} ToneOption;

typedef struct LoopOption {
    bool run;
    HalLoopCycles cycles;
} LoopOption;

typedef struct Core1Option {
    bool run;
    HalCore1Test test;
} Core1Option;

typedef struct Core1AccelOption {
    bool run;
    HalCore1Accel result;
} Core1AccelOption;

typedef struct BrowseEntry {
    BrowsePool *pool;
    const LibraryStore *store;
    Screen *unavailable;
    BrowseQuery query;
    bool favorites;
    Screen *fixed;
    SpokenKind shelf;
    bool present;
} BrowseEntry;

typedef struct AppScreensContext {
    AppOptions *options;
    const Theme *theme;
    Theme *styled_theme;
    const Theme *compact_theme;
    const LibraryStore *store;
    Player *player;
    SettingsStore *settings;
    FavoritesStore *favorites;
    PowerPolicy *power;
    Toast *toast;
    CoverCache *covers;
    bool *rescan_requested;
    bool *language_requested;
    SpokenStore *spoken;
    BluetoothLink *bluetooth;
} AppScreensContext;

typedef struct ViewsModuleScreens {
    CoverFlowScreen cover_flow;
    UpNextScreen up_next;
} ViewsModuleScreens;

typedef struct AppModuleScreens {
    ViewsModuleScreens views;
} AppModuleScreens;

typedef struct AppScreens {
    AppScreensContext context;
    uint32_t bound_modules;
    MenuScreen home;
    MenuScreen music;
    SettingsScreens settings;
    MenuScreen diagnostics;
    MessageScreen no_library;
    BrowsePool browse;
    NowPlayingScreen now_playing;
    BrowseEntry browse_entries[MUSIC_ITEM_COUNT];
    BrowseEntry shelf_entries[SHELF_COUNT];
    BluetoothScreen bluetooth;
    ClockOption clock;
    ToneOption tone;
    LoopOption loop;
    Core1Option core1;
    Core1AccelOption core1_accel;
    MenuItem home_items[HOME_ITEM_COUNT];
    HomeArt home_art;
    MenuItem music_items[MUSIC_ITEM_COUNT];
    MenuItem diagnostics_items[DIAGNOSTICS_ITEM_COUNT];
    HighRamOption high_ram;
    AppModuleScreens modules;
} AppScreens;

Screen *app_screens_init(AppScreens *screens, AppScreensContext context);
MenuItem app_screens_open_item(StringId label, Screen *target);
Screen *app_screens_build_menu(MenuScreen *menu, StringId title, const MenuItem *items, int count,
                               const Theme *theme);
void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme);
int app_screens_start_path(AppScreens *screens, StartScreen start, Screen **path);
Screen *app_screens_language_row(AppScreens *screens);
