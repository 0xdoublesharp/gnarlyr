#include "browse_sections.h"
#include "modules/modules.h"
#include "screens/cassette_now_playing.h"
#include "screens/meter_now_playing.h"
#include "settings_actions.h"

static void style_draw(const NowPlayingScreen *screen, Canvas *canvas)
{
    if (settings_now_playing_style(screen->config.settings) == NOW_PLAYING_CASSETTE) {
        cassette_now_playing_draw(screen, canvas);
    } else {
        meter_now_playing_draw(screen, canvas);
    }
}

static bool style_animate(NowPlayingScreen *screen, FrameDiff *diff, bool visible)
{
    return settings_now_playing_style(screen->config.settings) == NOW_PLAYING_CASSETTE
               ? cassette_now_playing_animate(screen, diff, visible)
               : meter_now_playing_animate(screen, diff, visible);
}

static Screen *open_artist(void *context, BrowseScreen *browse, BrowseQuery albums)
{
    AppScreens *screens = context;
    const Library *library = browse->pool->config.library;
    BrowseQuery album = albums;
    BrowseItem only;
    if (browse->query.kind != BROWSE_ARTISTS && browse->query.kind != BROWSE_ALBUM_ARTISTS) {
        return NULL;
    }
    if (browse_count(library, albums) == 1 && browse_item(library, albums, 0, &only) &&
        browse_child(albums, only, &album)) {
        return browse_pool_open(browse->pool, browse->level + 1, album);
    }
    CoverFlowScreen *strip = &screens->modules.playing.artist_albums;
    strip->albums = albums;
    strip->parent = browse;
    strip->positioned = false;
    return &strip->screen;
}

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    screens->browse.config.artist = open_artist;
    meter_now_playing_reset();
    screens->now_playing.config.cassette_draw = style_draw;
    screens->now_playing.config.cassette_animate = style_animate;
    PlayingModuleScreens *views = &screens->modules.playing;
    UpNextConfig up_next = {.library = &context->store->library,
                            .theme = context->theme,
                            .player = context->player,
                            .toast = context->toast,
                            .now_playing = &screens->now_playing.screen};
    CoverFlowConfig cover_flow = {
        .library = &context->store->library,
        .theme = context->theme,
        .covers = context->covers,
        .favorites = context->favorites,
        .toast = context->toast,
        .browse = &screens->browse,
        .player = context->player,
    };
    up_next_screen_init(&views->up_next, up_next);
    CoverFlowConfig artist_albums = cover_flow;
    artist_albums.artist_keys = app_screens_artist_keys;
    cover_flow_screen_init(&views->artist_albums, artist_albums);
    QuickSettingsConfig quick = {.settings = {.store = context->settings,
                                              .headphones = context->headphones,
                                              .player = context->player,
                                              .power = context->power,
                                              .toast = context->toast},
                                 .theme = context->theme,
                                 .lyrics = &screens->modules.views.lyrics.screen,
                                 .up_next = &views->up_next.screen,
                                 .speed = {.context = &views->quick_settings.config.settings,
                                           .available = settings_action_speed_available,
                                           .value = settings_action_speed_text,
                                           .cycle = settings_action_cycle_speed}};
    quick_settings_screen_init(&views->quick_settings, quick);
    cover_flow_screen_init(&views->cover_flow, cover_flow);
}

const PlayingModuleEntry PLAYING_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_PLAYING, bind),
    .section = browse_sections_at,
    .sections = browse_sections_range};
