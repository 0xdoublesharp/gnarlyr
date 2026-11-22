#include "favorite_playlist.h"
#include "hal/clock.h"
#include "mixtape_ui_private.h"
#include "modules/modules.h"
#include "screens/charge_screen.h"
#include "track_removal.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    ViewsModuleScreens *own = &screens->modules.views;
    theme_picker_init(&own->themes, screens);
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
    lyrics_screen_init(&own->lyrics, (LyricsScreenConfig){context->player, &context->store->library,
                                                          context->theme});
#ifdef ECHO_DEBUG_LOOP
    key_test_screen_init(&own->key_test, context->theme);
#endif
}

static Screen *manage(AppScreens *screens, uint16_t track, SongManage manage)
{
    const AppScreensContext *context = &screens->context;
    if (manage == SONG_MANAGE_TAPE) {
        return mixtape_ui_add(&screens->modules.views.mixtapes, track);
    }
    if (manage == SONG_MANAGE_FAVORITE) {
        FavoritePlaylistResult result = favorite_playlist_add(&context->store->library, track);
        toast_show(context->toast, favorite_playlist_message(result), hal_clock_milliseconds());
        return NULL;
    }
    TrackRemoval removal = {.tracks = {.store = context->store, .player = context->player},
                            .actions = &screens->browse.song_actions,
                            .toast = context->toast,
                            .question = &screens->modules.views.delete_question,
                            .theme = context->theme};
    return track_removal_manage(removal, track, manage);
}

const ViewsModuleEntry VIEWS_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_VIEWS, bind),
    .cancel = mixtape_ui_cancel,
    .step = mixtape_ui_step,
    .add = mixtape_ui_add,
    .charge_observe = charge_art_observe,
    .charge_draw = charge_art_draw,
    .theme_boot = theme_runtime_boot,
    .theme_restore = theme_runtime_restore,
    .manage = manage,
};
