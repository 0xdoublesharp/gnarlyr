#include "app/app.h"
#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    CardFormatConfig config = {.player = screens->context.player,
                               .theme = screens->context.theme,
                               .notice = app_show_notice,
                               .prepare = app_release_audio,
                               .finished = app_reload};
    card_format_screen_bind(&screens->modules.format.card, config);
#ifdef ECHO_DEBUG_LOOP
    key_test_screen_init(&screens->modules.format.key_test, screens->context.theme);
#endif
}

const ScreenModuleEntry FORMAT_MODULE_ENTRY = SCREEN_MODULE_ENTRY(SCREEN_MODULE_FORMAT, bind);
