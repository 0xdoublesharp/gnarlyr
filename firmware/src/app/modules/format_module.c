#include "app/app.h"
#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    CardFormatConfig config = {.player = screens->context.player,
                               .theme = screens->context.theme,
                               .notice = app_show_notice,
                               .prepare = app_release_audio,
                               .finished = app_reload};
    card_format_screen_bind(&screens->modules.format, config);
}

const ScreenModuleEntry FORMAT_MODULE_ENTRY = SCREEN_MODULE_ENTRY(SCREEN_MODULE_FORMAT, bind);
