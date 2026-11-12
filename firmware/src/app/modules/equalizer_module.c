#include "modules/modules.h"
#include "settings_actions.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    EqualizerUi *own = &screens->modules.equalizer;
    equalizer_ui_init(own, context->settings, context->player, context->toast, context->theme);
    QuickSettingsConfig quick = {.settings = {.store = context->settings,
                                              .headphones = context->headphones,
                                              .player = context->player,
                                              .power = context->power,
                                              .toast = context->toast},
                                 .theme = context->theme,
                                 .lyrics = &screens->modules.views.lyrics.screen,
                                 .up_next = &screens->modules.playing.up_next.screen,
                                 .speed = {.context = &own->quick_settings.config.settings,
                                           .available = settings_action_speed_available,
                                           .value = settings_action_speed_text,
                                           .cycle = settings_action_cycle_speed}};
    quick_settings_screen_init(&own->quick_settings, quick);
}

const ParametricModuleEntry EQUALIZER_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_EQUALIZER, bind),
    .load = parametric_store_read,
    .matching = parametric_store_slot};
