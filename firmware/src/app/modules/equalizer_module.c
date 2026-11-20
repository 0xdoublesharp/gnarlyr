#include "modules/modules.h"
#include "music_walk.h"

static bool rebuilt;

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    EqualizerUi *own = &screens->modules.equalizer;
    equalizer_ui_init(own, context->settings, context->player, context->toast, context->theme);
    QuickSettingsConfig quick = {.settings = {.store = context->settings,
                                              .headphones = context->headphones,
                                              .player = context->player,
                                              .power = context->power,
                                              .presets = own->presets,
                                              .toast = context->toast},
                                 .theme = context->theme,
                                 .lyrics = &screens->modules.views.lyrics.screen,
                                 .up_next = &screens->modules.playing.up_next.screen};
    quick_settings_screen_init(&own->quick_settings, quick);
    rebuilt = true;
}

static void reload(AppScreens *screens)
{
    if (rebuilt) {
        return;
    }
    rebuilt = true;
    EqualizerUi *own = &screens->modules.equalizer;
    own->presets->parametric = &screens->context.settings->parametric;
    equalizer_presets_scan(own->presets);
    equalizer_browser_refit(&own->browser);
    quick_settings_screen_refit(&own->quick_settings);
}

const ParametricModuleEntry EQUALIZER_MODULE_ENTRY = {
    .module = SCREEN_MODULE_RELOADING_ENTRY(SCREEN_MODULE_EQUALIZER, bind, reload),
    .load = parametric_store_read,
    .matching = parametric_store_slot,
    .music_walk = music_walk_run};
