#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    listening_screen_init(&screens->modules.listening.listening,
                          (ListeningScreenConfig){context->listening, context->theme});
    lyrics_screen_init(
        &screens->modules.listening.lyrics,
        (LyricsScreenConfig){context->player, &context->store->library, context->theme});
}

const ScreenModuleEntry LISTENING_MODULE_ENTRY = {
    SCREEN_MODULE_MAGIC(SCREEN_MODULE_LISTENING),
    bind,
};
