#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    theme_picker_init(&screens->modules.listening.themes, screens);
    const AppScreensContext *context = &screens->context;
    listening_screen_init(
        &screens->modules.listening.listening,
        (ListeningScreenConfig){context->listening, context->theme, context->motion});
    lyrics_screen_init(
        &screens->modules.listening.lyrics,
        (LyricsScreenConfig){context->player, &context->store->library, context->theme});
    smart_list_screen_init(&screens->modules.listening.smart_lists,
                           (SmartListScreenConfig){context->listening, &screens->browse});
}

const ListeningModuleEntry LISTENING_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_LISTENING), bind},
    .theme_boot = theme_runtime_boot,
    .theme_restore = theme_runtime_restore,
};
