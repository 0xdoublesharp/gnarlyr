#include "browse_sections.h"
#include "modules/modules.h"
#include "screens/meter_now_playing.h"

static void attach(NowPlayingScreen *now_playing)
{
    bool meter = settings_now_playing_style(now_playing->config.settings) >= NOW_PLAYING_VU;
    now_playing_screen_attach(now_playing);
    now_playing->config.cassette_draw = meter ? meter_now_playing_draw : NULL;
    now_playing->config.cassette_animate = meter ? meter_now_playing_animate : NULL;
}

static void bind(AppScreens *screens)
{
    (void)screens;
    meter_now_playing_reset();
}

const PlayingModuleEntry STAGE_MODULE_ENTRY = {.module =
                                                   SCREEN_MODULE_ENTRY(SCREEN_MODULE_STAGE, bind),
                                               .section = browse_sections_at,
                                               .sections = browse_sections_range,
                                               .now_playing_attach = attach,
                                               .wire_boot = wire_boot_fade};
