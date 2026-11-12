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
    MUSIC_FAVORITES,
    MUSIC_SMART
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
    {STR_SMART_LISTS, MUSIC_SMART, BROWSE_ALL_SONGS},
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

static ScreenAction open_smart_lists(void *context)
{
    AppScreens *screens = context;
    return screen_action_push(screens->context.store->available
                                  ? &screens->modules.listening.smart_lists.menu.screen
                                  : &screens->no_library.screen);
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

ScreenAction app_screens_artist_keys(void *context, KeyEvent event, uint32_t now_ms)
{
    CoverFlowScreen *strip = context;
    if (event.key != KEY_PLAY || event.gesture != KEY_GESTURE_LONG_PRESS) {
        return strip->keys(context, event, now_ms);
    }
    Screen *screen = browse_play_group(strip->config.browse->config.tools_context, strip->parent);
    return screen != NULL ? screen_action_push(screen) : screen_action_none();
}

static Screen *track_tape(void *context, uint16_t track)
{
    AppScreens *screens = context;
    const ViewsModuleEntry *entry =
        (const ViewsModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_VIEWS);
    return entry != NULL ? entry->add(&screens->modules.views.mixtapes, track) : NULL;
}

static int browse_section(const BrowseScreen *browse, int index)
{
    AppScreens *screens = browse->pool->config.tools_context;
    const PlayingModuleEntry *entry =
        (const PlayingModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_PLAYING);
    return entry != NULL ? entry->section(browse, index) : LIST_SECTION_BEFORE_LETTERS;
}

static ListSections browse_sections(const BrowseScreen *browse, int index)
{
    AppScreens *screens = browse->pool->config.tools_context;
    const PlayingModuleEntry *entry =
        (const PlayingModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_PLAYING);
    return entry != NULL ? entry->sections(browse, index) : (ListSections){0, 0};
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
    if (entry->source == MUSIC_SEARCH || entry->source == MUSIC_TAGS ||
        entry->source == MUSIC_SMART) {
        ScreenAction (*select)(void *) = entry->source == MUSIC_SEARCH ? open_search
                                         : entry->source == MUSIC_TAGS ? open_my_tags
                                                                       : open_smart_lists;
        return (MenuItem){
            .label = i18n_text(entry->label), .select = select, .context = screens, .opens = true};
    }
    screens->browse_entries[index] = (BrowseEntry){
        .pool = &screens->browse,
        .store = store,
        .unavailable = &screens->no_library.screen,
        .query = browse_root_query(entry->kind),
        .favorites = entry->source == MUSIC_FAVORITES,
        .fixed =
            entry->source == MUSIC_COVER_FLOW ? &screens->modules.playing.cover_flow.screen : NULL,
        .shelf = SPOKEN_NONE,
        .present = true,
    };
    return (MenuItem){.label = i18n_text(entry->label),
                      .select = open_browse,
                      .context = &screens->browse_entries[index],
                      .value = NULL,
                      .opens = true};
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
        .tape = track_tape,
        .play_group = browse_play_group,
        .section = browse_section,
        .sections = browse_sections,
        .tools_context = screens,
    };
    browse_pool_init(&screens->browse, browse);
    static const IconId icons[MUSIC_ITEM_COUNT] = {
        ICON_SEARCH, ICON_COVER_FLOW, ICON_ARTIST,   ICON_ALBUM_ARTIST, ICON_ALBUM, ICON_GENRE,
        ICON_FOLDER, ICON_SONG,       ICON_PLAYLIST, ICON_FAVORITE,     ICON_QUEUE, ICON_GENRE};
    for (int index = 0; index < MUSIC_ITEM_COUNT; index++) {
        screens->music_items[index] = music_item(screens, context.store, index);
        screens->music_items[index].icon = icons[index];
    }
    screens->music_items[MUSIC_ITEM_COUNT] = shelf_item(screens, context, SPOKEN_AUDIOBOOK);
    screens->music_items[MUSIC_ITEM_COUNT].icon = ICON_AUDIOBOOK;
    screens->music_items[MUSIC_ITEM_COUNT + 1] = shelf_item(screens, context, SPOKEN_PODCAST);
    screens->music_items[MUSIC_ITEM_COUNT + 1].icon = ICON_PODCAST;
    app_screens_build_menu(&screens->music, STR_MUSIC, screens->music_items, MUSIC_MENU_ITEM_COUNT,
                           context.theme);
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

const HomeItem APP_HOME_ITEMS[HOME_ITEM_COUNT] = {{STR_MUSIC, ICON_MUSIC},
                                                  {STR_AUDIOBOOKS, ICON_AUDIOBOOK},
                                                  {STR_PODCASTS, ICON_PODCAST},
                                                  {STR_SETTINGS, ICON_SETTINGS},
                                                  {STR_NOW_PLAYING, ICON_NOW_PLAYING},
                                                  {STR_UP_NEXT, ICON_QUEUE},
                                                  {STR_MIXTAPES, ICON_MIXTAPE},
                                                  {STR_LISTENING_STATS, ICON_DIAGNOSTICS},
                                                  {STR_USB_MODE, ICON_USB},
                                                  {STR_CLOCK, ICON_TIMEOUT}};

static void home_catalog(AppScreens *screens, MenuItem *items)
{
    AppScreensContext context = screens->context;
    Screen *targets[HOME_ITEM_COUNT] = {&screens->music.screen,
                                        NULL,
                                        NULL,
                                        &screens->modules.settings.settings.menu.screen,
                                        &screens->now_playing.screen,
                                        &screens->modules.playing.up_next.screen,
                                        &screens->modules.views.mixtapes.screen,
                                        &screens->modules.listening.listening.screen,
                                        NULL,
                                        &screens->modules.views.clock.screen};
    for (int index = 0; index < HOME_ITEM_COUNT; index++) {
        items[index] = app_screens_open_item(APP_HOME_ITEMS[index].label, targets[index]);
    }
    items[1] = shelf_item(screens, context, SPOKEN_AUDIOBOOK);
    items[2] = shelf_item(screens, context, SPOKEN_PODCAST);
    for (int index = 4; index <= 5; index++) {
        items[index].shown = player_has_track;
        items[index].shown_context = context.player;
    }
    items[8].select = open_usb_modes;
    items[8].context = screens;
    for (int index = 0; index < HOME_ITEM_COUNT; index++) {
        items[index].icon = APP_HOME_ITEMS[index].icon;
    }
}

static int arrange_home(AppScreens *screens)
{
    MenuItem items[HOME_ITEM_COUNT];
    home_catalog(screens, items);
    const Settings *settings = &screens->context.settings->settings;
    unsigned hidden = settings->home_hidden & ~(1u << SETTINGS_HOME_SETTINGS_ITEM);
    int count = 1;
    screens->home_items[0] = items[SETTINGS_HOME_NOW_PLAYING_ITEM];
    for (int index = 0; index < HOME_ITEM_COUNT; index++) {
        int item = settings->home_order[index];
        if (item != SETTINGS_HOME_NOW_PLAYING_ITEM && ((hidden >> item) & 1u) == 0) {
            screens->home_items[count++] = items[item];
        }
    }
    return count;
}

static bool home_shown(const MenuItem *item)
{
    return item->shown == NULL || item->shown(item->shown_context);
}

static const void *home_selected(const AppScreens *screens)
{
    int remaining = screens->home.list.selected;
    for (int index = 0; index < screens->home.config.item_count; index++) {
        const MenuItem *item = &screens->home_items[index];
        if (home_shown(item) && remaining-- == 0) {
            return item->context;
        }
    }
    return NULL;
}

static int home_position(const AppScreens *screens, const void *context)
{
    int position = 0;
    for (int index = 0; index < screens->home.config.item_count; index++) {
        const MenuItem *item = &screens->home_items[index];
        if (item->context == context) {
            return position;
        }
        position += home_shown(item) ? 1 : 0;
    }
    return 0;
}

void app_screens_arrange_home(AppScreens *screens)
{
    const void *selected = home_selected(screens);
    screens->home.config.item_count = arrange_home(screens);
    list_view_select(&screens->home.list, home_position(screens, selected), 0);
}

static void build_home(AppScreens *screens, AppScreensContext context)
{
    home_art_init(&screens->home_art, context.covers, context.store, context.player, context.theme);
    MenuConfig home = {.title = FIRMWARE_SHORT_NAME,
                       .items = screens->home_items,
                       .item_count = arrange_home(screens),
                       .theme = context.theme,
                       .panel = {.width = BOARD_LAYOUT.home_art_width,
                                 .min_height = BOARD_LAYOUT.home_art_min_height,
                                 .context = &screens->home_art}};
    menu_screen_init(&screens->home, home);
    screen_modules_prepare_home(screens);
}

Screen *app_screens_init(AppScreens *screens, AppScreensContext context)
{
    screens->shortcut_destination = NULL;
    const Theme *theme = context.theme;
    MessageConfig no_library = {.title = i18n_text(STR_NO_LIBRARY),
                                .message = i18n_text(STR_NO_LIBRARY_HINT),
                                .theme = theme};
    NowPlayingConfig now_playing = {.player = context.player,
                                    .settings = &context.settings->settings,
                                    .library = &context.store->library,
                                    .theme = context.compact_theme,
                                    .covers = context.covers,
                                    .quick_settings =
                                        &screens->modules.playing.quick_settings.screen,
                                    .lyrics = &screens->modules.listening.lyrics.screen,
                                    .flip = &screens->modules.views.cassette_flip.screen};
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
                             ? home_position(screens, &screens->now_playing.screen)
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
