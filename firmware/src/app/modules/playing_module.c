#include "artist_album.h"
#include "browse_sections.h"
#include "modules/modules.h"
#include "screens/cassette_now_playing.h"
#include "settings_actions.h"

static void attach(NowPlayingScreen *now_playing)
{
    bool cassette =
        settings_now_playing_style(now_playing->config.settings) == NOW_PLAYING_CASSETTE;
    now_playing_screen_attach(now_playing);
    now_playing->config.cassette_draw = cassette ? cassette_now_playing_draw : NULL;
    now_playing->config.cassette_animate = cassette ? cassette_now_playing_animate : NULL;
}

static Screen *artist_strip(AppScreens *screens, BrowseScreen *browse, BrowseQuery albums)
{
    CoverFlowScreen *strip = &screens->modules.playing.artist_albums;
    strip->albums = albums;
    strip->parent = browse;
    strip->positioned = false;
    return &strip->screen;
}

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
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
    cover_flow_screen_init(&views->cover_flow, cover_flow);
}

const PlayingModuleEntry PLAYING_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_PLAYING, bind),
    .section = browse_sections_at,
    .sections = browse_sections_range,
    .now_playing_attach = attach,
    .wire_boot = wire_boot_fade,
    .artist_strip = artist_strip,
    .only_album = artist_album_only};
