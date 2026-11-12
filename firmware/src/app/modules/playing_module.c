#include "modules/modules.h"
#include "screens/charge_screen.h"
#include "settings_actions.h"

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
    QuickSettingsConfig quick = {.settings = {.store = context->settings,
                                              .headphones = context->headphones,
                                              .player = context->player,
                                              .power = context->power,
                                              .toast = context->toast},
                                 .theme = context->theme,
                                 .lyrics = &screens->modules.listening.lyrics.screen,
                                 .up_next = &views->up_next.screen,
                                 .speed = {.context = &views->quick_settings.config.settings,
                                           .available = settings_action_speed_available,
                                           .value = settings_action_speed_text,
                                           .cycle = settings_action_cycle_speed}};
    quick_settings_screen_init(&views->quick_settings, quick);
    cover_flow_screen_init(&views->cover_flow, cover_flow);
}

const PlayingModuleEntry PLAYING_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_PLAYING, bind), .charge_draw = charge_art_draw};
