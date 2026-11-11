#include "modules/modules.h"
#include "music_walk.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "discovery_playback.h"

static DiscoveryScreen *discovery_of(AppScreens *screens)
{
    return &screens->modules.library.discovery;
}

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    discovery_playback_cancel(discovery_of(screens));
    discovery_screen_init(discovery_of(screens),
                          (DiscoveryScreenConfig){.library = &context->store->library,
                                                  .player = context->player,
                                                  .theme = context->theme,
                                                  .now_playing = &screens->now_playing.screen,
                                                  .motion = context->motion});
}

static void clear_scope(DiscoveryScreen *screen)
{
    screen->config.scope_accept = NULL;
    screen->config.scope_context = NULL;
    screen->config.scope_title = NULL;
    screen->scope_browse = NULL;
    screen->scope_favorites = false;
}

static LibraryViewAcceptance scope_accept(void *context, uint16_t id, const LibraryTrack *track)
{
    DiscoveryScreen *screen = context;
    BrowseQuery query = screen->scope_query;
    const Library *library = screen->config.library;
    (void)id;
    if (screen->scope_favorites) {
        const FavoritesStore *favorites = screen->scope_browse->pool->config.favorites;
        return favorites != NULL && favorites_store_contains_album(favorites, track->album)
                   ? LIBRARY_VIEW_ACCEPT
                   : LIBRARY_VIEW_REJECT;
    }
    if (query.kind != BROWSE_GROUP_ALBUMS) {
        return LIBRARY_VIEW_ACCEPT;
    }
    if (query.group == LIBRARY_ARTISTS) {
        return track->artist == query.subject ? LIBRARY_VIEW_ACCEPT : LIBRARY_VIEW_REJECT;
    }
    if (query.group == LIBRARY_GENRES) {
        return track->genre == query.subject ? LIBRARY_VIEW_ACCEPT : LIBRARY_VIEW_REJECT;
    }
    LibraryAlbum album;
    if (!library_read_album(library, track->album, &album)) {
        return LIBRARY_VIEW_ACCEPT_FAILED;
    }
    return album.album_artist == query.subject ? LIBRARY_VIEW_ACCEPT : LIBRARY_VIEW_REJECT;
}

static BrowseQuery scope_source(DiscoveryScreen *screen, BrowseQuery query)
{
    screen->scope_query = query;
    if (query.kind == BROWSE_FOLDER) {
        query.kind = BROWSE_FOLDER_TREE;
    } else if (query.kind != BROWSE_ALL_SONGS && query.kind != BROWSE_ALBUM_TRACKS &&
               query.kind != BROWSE_PLAYLIST_TRACKS && query.kind != BROWSE_FOLDER_TREE) {
        query = browse_root_query(BROWSE_ALL_SONGS);
    }
    if (screen->scope_favorites || screen->scope_query.kind == BROWSE_GROUP_ALBUMS) {
        screen->config.scope_accept = scope_accept;
        screen->config.scope_context = screen;
    }
    return query;
}

static void name_scope(DiscoveryScreen *screen, const char *title)
{
    StringBuilder builder =
        string_builder_make(screen->scope_title_text, sizeof screen->scope_title_text);
    string_builder_append(&builder, title);
    screen->config.scope_title = screen->scope_title_text;
}

static Screen *open(AppScreens *screens, BrowseQuery source, bool edit_search)
{
    DiscoveryScreen *screen = discovery_of(screens);
    clear_scope(screen);
    return discovery_screen_open(screen, scope_source(screen, source), edit_search);
}

static Screen *options(AppScreens *screens, BrowseQuery source)
{
    DiscoveryScreen *screen = discovery_of(screens);
    clear_scope(screen);
    browse_title(screen->config.library, source, screen->scope_title_text,
                 sizeof screen->scope_title_text);
    screen->config.scope_title = screen->scope_title_text;
    return discovery_screen_options(screen, scope_source(screen, source));
}

static Screen *info(AppScreens *screens, uint16_t track)
{
    return discovery_screen_info(discovery_of(screens), track);
}

static Screen *tags(AppScreens *screens, uint16_t track)
{
    return discovery_screen_tags(discovery_of(screens), track);
}

static bool chosen_item(BrowseScreen *browse, BrowseItem *item)
{
    uint32_t position = (uint32_t)browse->list.selected;
    if (browse->favorites_only) {
        position = browse->pool->favorite_albums[position];
    }
    return browse_item(browse->pool->config.library, browse->query, position, item);
}

static Screen *browse_view(AppScreens *screens, BrowseScreen *browse, bool play_group)
{
    DiscoveryScreen *screen = discovery_of(screens);
    BrowseQuery source = browse->query;
    BrowseItem selected;
    bool chosen = chosen_item(browse, &selected);
    if (!play_group && source.kind == BROWSE_PLAYLISTS) {
        if (screens->context.toast != NULL) {
            toast_show(screens->context.toast, i18n_text(STR_OPEN_PLAYLIST_FIRST), browse->now_ms);
        }
        return NULL;
    }
    clear_scope(screen);
    screen->scope_browse = browse;
    screen->scope_favorites = browse->favorites_only;
    if (play_group) {
        if (!chosen || selected.kind != BROWSE_ITEM_GROUP ||
            !browse_child(source, selected, &source)) {
            return NULL;
        }
        char title[BROWSE_LABEL_CAPACITY];
        char detail[BROWSE_DETAIL_CAPACITY];
        if (!browse_item_text(screen->config.library, browse->query, selected,
                              (BrowseText){title, sizeof title, detail, sizeof detail})) {
            return NULL;
        }
        name_scope(screen, title);
        screen->query = library_view_defaults();
        screen->required_tags = 0;
    } else {
        name_scope(screen, browse->title);
        if (chosen && selected.kind == BROWSE_ITEM_TRACK) {
            screen->selected_track = selected.index;
        }
    }
    source = scope_source(screen, source);
    Screen *result = play_group ? discovery_screen_open(screen, source, false)
                                : discovery_screen_options(screen, source);
    screen->auto_play = play_group;
    return result;
}

static void step(AppScreens *screens, uint32_t now_ms)
{
    discovery_playback_step(discovery_of(screens), now_ms);
}

static void cancel(AppScreens *screens)
{
    discovery_playback_cancel(discovery_of(screens));
}

const LibraryModuleEntry LIBRARY_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_LIBRARY), bind},
    .music_fingerprint = music_walk_fingerprint,
    .open = open,
    .options = options,
    .info = info,
    .tags = tags,
    .browse = browse_view,
    .step = step,
    .cancel = cancel,
};
