#include "modules/modules.h"
#include "screens/charge_screen.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    ViewsModuleScreens *views = &screens->modules.views;
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
    cover_flow_screen_init(&views->cover_flow, cover_flow);
}

const ViewsModuleEntry VIEWS_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_VIEWS), bind}, .charge_draw = charge_art_draw};
