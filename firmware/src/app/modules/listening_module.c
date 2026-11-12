#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    theme_picker_init(&screens->modules.listening.themes, screens);
    const AppScreensContext *context = &screens->context;
    const Settings *settings = context->settings != NULL ? &context->settings->settings : NULL;
    listening_screen_init(
        &screens->modules.listening.listening,
        (ListeningScreenConfig){context->listening, context->theme, context->motion, context->toast,
                                context->rescan_requested, context->covers, settings});
    screens->modules.listening.listening.moments =
        moments_screen_init(&screens->modules.listening.moments,
                            (MomentsScreenConfig){context->player, context->theme, context->toast,
                                                  &screens->now_playing.screen, context->motion});
    smart_list_screen_init(&screens->modules.listening.smart_lists,
                           (SmartListScreenConfig){context->listening, &screens->browse});
}

const ListeningModuleEntry LISTENING_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_LISTENING, bind),
    .theme_boot = theme_runtime_boot,
    .theme_restore = theme_runtime_restore,
};
