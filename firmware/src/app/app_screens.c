#include "app_screens.h"

#include <stdbool.h>
#include <stddef.h>

#include "board_layout.h"
#include "core/i18n.h"
#include "diagnostics_values.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"
#include "modules/modules.h"

typedef enum MusicSource {
    MUSIC_SEARCH,
    MUSIC_TAGS,
    MUSIC_COVER_FLOW,
    MUSIC_LIBRARY,
    MUSIC_FAVORITES
} MusicSource;

typedef struct MusicEntry {
    StringId label;
    MusicSource source;
    BrowseKind kind;
} MusicEntry;

enum {
    MUSIC_COVER_FLOW_ENTRY = 1,
    MUSIC_ARTISTS_ENTRY = 2,
    MUSIC_ALBUMS_ENTRY = 4,
    MUSIC_PLAYLISTS_ENTRY = 8,
    MUSIC_FAVORITES_ENTRY = 9
};

static const MusicEntry MUSIC_ENTRIES[MUSIC_ITEM_COUNT] = {
    {STR_SEARCH, MUSIC_SEARCH, BROWSE_ALL_SONGS},
    {STR_COVER_FLOW, MUSIC_COVER_FLOW, BROWSE_ALBUMS},
    {STR_ARTISTS, MUSIC_LIBRARY, BROWSE_ARTISTS},
    {STR_ALBUM_ARTISTS, MUSIC_LIBRARY, BROWSE_ALBUM_ARTISTS},
    {STR_ALBUMS, MUSIC_LIBRARY, BROWSE_ALBUMS},
    {STR_GENRES, MUSIC_LIBRARY, BROWSE_GENRES},
    {STR_FOLDERS, MUSIC_LIBRARY, BROWSE_FOLDER},
    {STR_ALL_SONGS, MUSIC_LIBRARY, BROWSE_ALL_SONGS},
    {STR_PLAYLISTS, MUSIC_LIBRARY, BROWSE_PLAYLISTS},
    {STR_FAVORITES, MUSIC_FAVORITES, BROWSE_ALBUMS},
    {STR_PERSONAL_TAGS, MUSIC_TAGS, BROWSE_ALL_SONGS},
};

static bool player_has_track(const void *context)
{
    return player_status(context).has_track;
}

MenuItem app_screens_open_item(StringId label, Screen *target)
{
    return (MenuItem){.label = i18n_text(label),
                      .select = menu_open_screen,
                      .context = target,
                      .value = NULL,
                      .opens = true};
}

Screen *app_screens_build_menu(MenuScreen *menu, StringId title, const MenuItem *items, int count,
                               const Theme *theme)
{
    return menu_screen_init(
        menu, (MenuConfig){
                  .title = i18n_text(title), .items = items, .item_count = count, .theme = theme});
}

static Screen *browse_screen_for(BrowseEntry *entry)
{
    if (!entry->store->available) {
        return entry->unavailable;
    }
    if (entry->fixed != NULL) {
        return entry->fixed;
    }
    if (entry->favorites) {
        return browse_pool_open_favorites(entry->pool, 0);
    }
    if (entry->shelf != SPOKEN_NONE) {
        return browse_pool_open_shelf(entry->pool, 0, entry->shelf);
    }
    return browse_pool_open(entry->pool, 0, entry->query);
}

static ScreenAction open_browse(void *context)
{
    return screen_action_push(browse_screen_for(context));
}

static const LibraryModuleEntry *library_tools(AppScreens *screens)
{
    const LibraryModuleEntry *entry =
        (const LibraryModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_LIBRARY);
    if (entry == NULL && screens->context.toast != NULL) {
        StringId message =
            hal_system_restart_pending() ? STR_RESTART_TO_FINISH : STR_SCREEN_MISSING;
        toast_show(screens->context.toast, i18n_text(message), hal_clock_milliseconds());
    }
    return entry;
}

static ScreenAction open_search(void *context)
{
    AppScreens *screens = context;
    if (!screens->context.store->available) {
        return screen_action_push(&screens->no_library.screen);
    }
    const LibraryModuleEntry *entry = library_tools(screens);
    return entry != NULL
               ? screen_action_push(entry->open(screens, browse_root_query(BROWSE_ALL_SONGS), true))
               : screen_action_none();
}

static ScreenAction open_my_tags(void *context)
{
    AppScreens *screens = context;
    const LibraryModuleEntry *entry = library_tools(screens);
    return entry != NULL ? screen_action_push(entry->tags(screens, LIBRARY_NONE))
                         : screen_action_none();
}

static Screen *browse_options(void *context, BrowseScreen *browse)
{
    AppScreens *screens = context;
    const LibraryModuleEntry *entry = library_tools(screens);
    return entry != NULL ? entry->browse(screens, browse, false) : NULL;
}

static Screen *browse_play_group(void *context, BrowseScreen *browse)
{
    AppScreens *screens = context;
    const LibraryModuleEntry *entry = library_tools(screens);
    return entry != NULL ? entry->browse(screens, browse, true) : NULL;
}

static Screen *track_info(void *context, uint16_t track)
{
    AppScreens *screens = context;
    const LibraryModuleEntry *entry = library_tools(screens);
    return entry != NULL ? entry->info(screens, track) : NULL;
}

static Screen *track_tags(void *context, uint16_t track)
{
    AppScreens *screens = context;
    const LibraryModuleEntry *entry = library_tools(screens);
    return entry != NULL ? entry->tags(screens, track) : NULL;
}

static MenuItem music_item(AppScreens *screens, const LibraryStore *store, int index)
{
    const MusicEntry *entry = &MUSIC_ENTRIES[index];
    if (entry->source == MUSIC_SEARCH || entry->source == MUSIC_TAGS) {
        return (MenuItem){.label = i18n_text(entry->label),
                          .select = entry->source == MUSIC_SEARCH ? open_search : open_my_tags,
                          .context = screens,
                          .opens = true};
    }
    screens->browse_entries[index] = (BrowseEntry){
        .pool = &screens->browse,
        .store = store,
        .unavailable = &screens->no_library.screen,
        .query = browse_root_query(entry->kind),
        .favorites = entry->source == MUSIC_FAVORITES,
        .fixed =
            entry->source == MUSIC_COVER_FLOW ? &screens->modules.views.cover_flow.screen : NULL,
        .shelf = SPOKEN_NONE,
        .present = true,
    };
    return (MenuItem){.label = i18n_text(entry->label),
                      .select = open_browse,
                      .context = &screens->browse_entries[index],
                      .value = NULL,
                      .opens = true};
}

static void build_music(AppScreens *screens, AppScreensContext context)
{
    BrowsePoolConfig browse = {
        .library = &context.store->library,
        .theme = context.theme,
        .player = context.player,
        .now_playing = &screens->now_playing.screen,
        .favorites = context.favorites,
        .toast = context.toast,
        .spoken = context.spoken,
        .options = browse_options,
        .info = track_info,
        .tags = track_tags,
        .play_group = browse_play_group,
        .tools_context = screens,
    };
    browse_pool_init(&screens->browse, browse);
    static const IconId icons[MUSIC_ITEM_COUNT] = {
        ICON_SEARCH, ICON_COVER_FLOW, ICON_ARTIST,   ICON_ALBUM_ARTIST, ICON_ALBUM, ICON_GENRE,
        ICON_FOLDER, ICON_SONG,       ICON_PLAYLIST, ICON_FAVORITE,     ICON_GENRE};
    for (int index = 0; index < MUSIC_ITEM_COUNT; index++) {
        screens->music_items[index] = music_item(screens, context.store, index);
        screens->music_items[index].icon = icons[index];
    }
    app_screens_build_menu(&screens->music, STR_MUSIC, screens->music_items, MUSIC_ITEM_COUNT,
                           context.theme);
}

static bool shelf_present(const void *context)
{
    const BrowseEntry *entry = context;
    return entry->present;
}

static MenuItem shelf_item(AppScreens *screens, AppScreensContext context, SpokenKind shelf)
{
    BrowseEntry *entry = &screens->shelf_entries[shelf == SPOKEN_PODCAST ? 1 : 0];
    *entry = (BrowseEntry){
        .pool = &screens->browse,
        .store = context.store,
        .unavailable = &screens->no_library.screen,
        .shelf = shelf,
        .present = context.store->available && spoken_shelf_present(&context.store->library, shelf),
    };
    return (MenuItem){.label = i18n_text(shelf == SPOKEN_PODCAST ? STR_PODCASTS : STR_AUDIOBOOKS),
                      .select = open_browse,
                      .context = entry,
                      .opens = true,
                      .shown = shelf_present,
                      .shown_context = entry};
}

Screen *app_screens_usb_modes(AppScreens *screens, bool prompt)
{
    const SettingsModuleEntry *entry =
        (const SettingsModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_SETTINGS);
    return entry != NULL ? entry->usb_modes(screens, prompt) : NULL;
}

static ScreenAction open_usb_modes(void *context)
{
    Screen *screen = app_screens_usb_modes(context, false);
    return screen != NULL ? screen_action_push(screen) : screen_action_none();
}

static void build_home(AppScreens *screens, AppScreensContext context)
{
    const Theme *theme = context.theme;
    screens->home_items[0] = app_screens_open_item(STR_MUSIC, &screens->music.screen);
    screens->home_items[6] =
        app_screens_open_item(STR_MIXTAPES, &screens->modules.views.mixtapes.screen);
    screens->home_items[7] =
        app_screens_open_item(STR_LISTENING, &screens->modules.listening.listening.screen);
    screens->home_items[1] = shelf_item(screens, context, SPOKEN_AUDIOBOOK);
    screens->home_items[2] = shelf_item(screens, context, SPOKEN_PODCAST);
    screens->home_items[3] =
        app_screens_open_item(STR_SETTINGS, &screens->modules.settings.settings.menu.screen);
    screens->home_items[4] = app_screens_open_item(STR_NOW_PLAYING, &screens->now_playing.screen);
    screens->home_items[4].shown = player_has_track;
    screens->home_items[4].shown_context = context.player;
    screens->home_items[5] =
        app_screens_open_item(STR_UP_NEXT, &screens->modules.views.up_next.screen);
    screens->home_items[5].shown = player_has_track;
    screens->home_items[5].shown_context = context.player;
    screens->home_items[8] = (MenuItem){.label = i18n_text(STR_USB_MODE),
                                        .select = open_usb_modes,
                                        .context = screens,
                                        .opens = true};
    static const IconId icons[HOME_ITEM_COUNT] = {ICON_MUSIC,    ICON_AUDIOBOOK,   ICON_PODCAST,
                                                  ICON_SETTINGS, ICON_NOW_PLAYING, ICON_QUEUE,
                                                  ICON_MIXTAPE,  ICON_DIAGNOSTICS, ICON_USB};
    for (int index = 0; index < HOME_ITEM_COUNT; index++) {
        screens->home_items[index].icon = icons[index];
    }
    home_art_init(&screens->home_art, context.covers, context.store, context.player, theme);
    MenuConfig home = {.title = FIRMWARE_SHORT_NAME,
                       .items = screens->home_items,
                       .item_count = HOME_ITEM_COUNT,
                       .theme = context.theme,
                       .panel = {.width = BOARD_LAYOUT.home_art_width,
                                 .min_height = BOARD_LAYOUT.home_art_min_height,
                                 .update = home_art_update,
                                 .draw = home_art_draw,
                                 .context = &screens->home_art}};
    menu_screen_init(&screens->home, home);
}

Screen *app_screens_init(AppScreens *screens, AppScreensContext context)
{
    const Theme *theme = context.theme;
    MessageConfig no_library = {.title = i18n_text(STR_NO_LIBRARY),
                                .message = i18n_text(STR_NO_LIBRARY_HINT),
                                .theme = theme};
    NowPlayingConfig now_playing = {.player = context.player,
                                    .settings = &context.settings->settings,
                                    .library = &context.store->library,
                                    .theme = context.compact_theme,
                                    .covers = context.covers,
                                    .up_next = &screens->modules.views.up_next.screen,
                                    .lyrics = &screens->modules.listening.lyrics.screen};
    screens->context = context;
    screens->bound_modules = 0;
    message_screen_init(&screens->no_library, no_library);
    now_playing_screen_init(&screens->now_playing, now_playing);
    build_music(screens, context);
    build_home(screens, context);
    return &screens->home.screen;
}

static int music_path(AppScreens *screens, int entry, Screen **path)
{
    BrowseEntry *browse = &screens->browse_entries[entry];
    if (!browse->store->available) {
        return 0;
    }
    list_view_select(&screens->music.list, entry, 0);
    path[0] = &screens->music.screen;
    path[1] = browse_screen_for(browse);
    return path[1] != NULL ? START_PATH_CAPACITY : 1;
}

int app_screens_start_path(AppScreens *screens, StartScreen start, Screen **path)
{
    switch (start) {
    case START_MAIN_MENU:
        return 0;
    case START_NOW_PLAYING:
        list_view_select(&screens->home.list,
                         player_status(screens->context.player).has_track
                             ? 2 + (screens->shelf_entries[0].present ? 1 : 0) +
                                   (screens->shelf_entries[1].present ? 1 : 0)
                             : 0,
                         0);
        path[0] = &screens->now_playing.screen;
        return 1;
    case START_ALBUMS:
        return music_path(screens, MUSIC_ALBUMS_ENTRY, path);
    case START_ARTISTS:
        return music_path(screens, MUSIC_ARTISTS_ENTRY, path);
    case START_FAVORITES:
        return music_path(screens, MUSIC_FAVORITES_ENTRY, path);
    case START_PLAYLISTS:
        return music_path(screens, MUSIC_PLAYLISTS_ENTRY, path);
    case START_COVER_FLOW:
        return music_path(screens, MUSIC_COVER_FLOW_ENTRY, path);
    }
    return 0;
}
