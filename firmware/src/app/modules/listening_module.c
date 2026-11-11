#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    listening_screen_init(&screens->modules.listening.listening,
                          (ListeningScreenConfig){context->listening, context->theme,
                                                  context->motion, context->toast,
                                                  context->rescan_requested});
    screens->modules.listening.listening.moments =
        moments_screen_init(&screens->modules.listening.moments,
                            (MomentsScreenConfig){context->player, context->theme, context->toast,
                                                  &screens->now_playing.screen, context->motion});
    lyrics_screen_init(
        &screens->modules.listening.lyrics,
        (LyricsScreenConfig){context->player, &context->store->library, context->theme});
    smart_list_screen_init(&screens->modules.listening.smart_lists,
                           (SmartListScreenConfig){context->listening, &screens->browse});
}

const ScreenModuleEntry LISTENING_MODULE_ENTRY = {
    SCREEN_MODULE_MAGIC(SCREEN_MODULE_LISTENING),
    bind,
};
