#pragma once

#include <stdbool.h>

#include "browse_screen.h"
#include "core/i18n.h"
#include "core/power_policy.h"
#include "cover_cache.h"
#include "debug_loop_internal.h"
#include "diagnostics_values.h"
#include "discovery_screen.h"
#include "equalizer_ui.h"
#include "favorites_store.h"
#include "hal/audio.h"
#include "home_art.h"
#include "home_editor.h"
#include "library_store.h"
#include "listening_store.h"
#include "mixtape_ui.h"
#include "player.h"
#include "screen_modules.h"
#include "screens/bluetooth_screen.h"
#include "screens/calendar_screen.h"
#include "screens/cassette_flip_screen.h"
#include "screens/clock_screen.h"
#include "screens/cover_flow_screen.h"
#include "screens/listening_screen.h"
#include "screens/lyrics_screen.h"
#include "screens/moments_screen.h"
#include "screens/now_playing_screen.h"
#include "screens/quick_settings_screen.h"
#include "screens/smart_list_screen.h"
#include "screens/up_next_screen.h"
#include "settings/settings.h"
#include "settings_screens.h"
#include "settings_store.h"
#include "ui/menu_screen.h"
#include "ui/message_screen.h"
#include "ui/toast.h"

enum {
    HOME_ITEM_COUNT = SETTINGS_HOME_ITEM_COUNT,
    SHELF_COUNT = 2,
    MUSIC_ITEM_COUNT = 12,
    MUSIC_MENU_ITEM_COUNT = MUSIC_ITEM_COUNT + 2,
    DIAGNOSTICS_ITEM_COUNT = 11 + DEBUG_LOOP_ABOUT_ROWS,
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
    HeadphoneStore *headphones;
    FavoritesStore *favorites;
    PowerPolicy *power;
    Toast *toast;
    CoverCache *covers;
    bool *rescan_requested;
    bool *language_requested;
    SpokenStore *spoken;
    BluetoothLink *bluetooth;
    ListeningStore *listening;
    WireMotion *motion;
} AppScreensContext;

typedef struct PlayingModuleScreens {
    CoverFlowScreen cover_flow;
    CoverFlowScreen artist_albums;
    UpNextScreen up_next;
    QuickSettingsScreen quick_settings;
} PlayingModuleScreens;

typedef struct ViewsModuleScreens {
    MixtapeUi mixtapes;
    CassetteFlipScreen cassette_flip;
    ClockScreen clock;
    CalendarScreen calendar;
    MenuScreen diagnostics;
    ClockOption diagnostic_clock;
    MenuItem diagnostics_items[DIAGNOSTICS_ITEM_COUNT];
} ViewsModuleScreens;

typedef struct SettingsModuleScreens {
    SettingsScreens settings;
    BluetoothScreen bluetooth;
    HomeEditor home_editor;
} SettingsModuleScreens;

typedef struct ProfileModuleScreens {
    ListeningScreen listening;
    MomentsScreen moments;
    LyricsScreen lyrics;
    SmartListScreen smart_lists;
} ProfileModuleScreens;

typedef struct LibraryModuleScreens {
    DiscoveryScreen discovery;
} LibraryModuleScreens;

typedef struct AppModuleScreens {
    PlayingModuleScreens playing;
    EqualizerUi equalizer;
    ViewsModuleScreens views;
    SettingsModuleScreens settings;
    ProfileModuleScreens listening;
    LibraryModuleScreens library;
} AppModuleScreens;

typedef struct AppScreens {
    AppScreensContext context;
    uint32_t bound_modules;
    MenuScreen home;
    MenuScreen music;
    MessageScreen no_library;
    BrowsePool browse;
    NowPlayingScreen now_playing;
    BrowseEntry browse_entries[MUSIC_ITEM_COUNT];
    BrowseEntry shelf_entries[SHELF_COUNT];
    ToneOption tone;
    LoopOption loop;
    Core1Option core1;
    Core1AccelOption core1_accel;
    MenuItem home_items[HOME_ITEM_COUNT];
    HomeArt home_art;
    MenuItem music_items[MUSIC_MENU_ITEM_COUNT];
    HighRamOption high_ram;
    AppModuleScreens modules;
    Screen *shortcut_destination;
} AppScreens;

extern const HomeItem APP_HOME_ITEMS[HOME_ITEM_COUNT];

Screen *app_screens_init(AppScreens *screens, AppScreensContext context);
void app_screens_arrange_home(AppScreens *screens);
Screen *app_screens_usb_modes(AppScreens *screens, bool prompt);
MenuItem app_screens_open_item(StringId label, Screen *target);
Screen *app_screens_build_menu(MenuScreen *menu, StringId title, const MenuItem *items, int count,
                               const Theme *theme);
void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme);
int app_screens_start_path(AppScreens *screens, StartScreen start, Screen **path);
ScreenAction app_screens_artist_keys(void *context, KeyEvent event, uint32_t now_ms);
