#include "browse_sections.h"
#include "mixtape_ui_private.h"
#include "modules/modules.h"

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
    ViewsModuleScreens *own = &screens->modules.views;
    screens->browse.config.artist = open_artist;
    diagnostics_menu_build(screens, context->options, context->theme);
    calendar_screen_init(&own->calendar, (CalendarScreenConfig){.theme = context->theme});
    clock_screen_init(&own->clock, (ClockScreenConfig){.theme = context->theme,
                                                       .settings = &context->settings->settings,
                                                       .calendar = &own->calendar.screen});
    cassette_flip_screen_init(&own->cassette_flip, context->player, context->theme, &own->mixtapes);
    mixtape_ui_api()->init(&own->mixtapes,
                           (MixtapeUiConfig){.player = context->player,
                                             .theme = context->theme,
                                             .toast = context->toast,
                                             .now_playing = &screens->now_playing.screen,
                                             .up_next = &screens->modules.playing.up_next.screen,
                                             .motion = context->motion});
}

const ViewsModuleEntry VIEWS_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_VIEWS, bind),
    .cancel = mixtape_ui_cancel,
    .step = mixtape_ui_step,
    .add = mixtape_ui_add,
    .section = browse_sections_at,
    .sections = browse_sections_range,
};
