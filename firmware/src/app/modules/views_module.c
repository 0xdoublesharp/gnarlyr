#include "mixtape_ui_private.h"
#include "modules/modules.h"
#include "screens/charge_screen.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    ViewsModuleScreens *views = &screens->modules.views;
    diagnostics_menu_build(screens, context->options, context->theme);
    UpNextConfig up_next = {.library = &context->store->library,
                            .theme = context->theme,
                            .player = context->player,
                            .toast = context->toast};
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
    calendar_screen_init(&views->calendar, (CalendarScreenConfig){.theme = context->theme});
    clock_screen_init(&views->clock, (ClockScreenConfig){.theme = context->theme,
                                                         .settings = &context->settings->settings,
                                                         .calendar = &views->calendar.screen});
    cover_flow_screen_init(&views->cover_flow, cover_flow);
    cassette_flip_screen_init(&views->cassette_flip, context->player, context->theme,
                              &views->mixtapes);
    mixtape_ui_api()->init(&views->mixtapes,
                           (MixtapeUiConfig){.player = context->player,
                                             .theme = context->theme,
                                             .toast = context->toast,
                                             .now_playing = &screens->now_playing.screen,
                                             .up_next = &views->up_next.screen,
                                             .motion = context->motion});
}

const ViewsModuleEntry VIEWS_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_VIEWS), bind},
    .cancel = mixtape_ui_cancel,
    .step = mixtape_ui_step,
    .add = mixtape_ui_add,
    .charge_draw = charge_art_draw};
