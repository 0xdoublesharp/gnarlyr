#include "browse_sections.h"
#include "mixtape_ui_private.h"
#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    ViewsModuleScreens *own = &screens->modules.views;
    theme_picker_init(&own->themes, screens);
    diagnostics_menu_build(screens, context->options, context->theme);
    calendar_screen_init(&own->calendar, (CalendarScreenConfig){.theme = context->theme});
    clock_screen_init(&own->clock, (ClockScreenConfig){.theme = context->theme,
                                                       .settings = &context->settings->settings,
                                                       .calendar = &own->calendar.screen});
    cassette_flip_screen_init(&own->cassette_flip, context->player, context->theme, &own->mixtapes);
    mixtape_ui_api()->init(&own->mixtapes,
                           (MixtapeUiConfig){.player = context->player,
                                             .theme = context->theme,
                                             .toast = context->toast,
                                             .now_playing = &screens->now_playing.screen,
                                             .up_next = &screens->modules.playing.up_next.screen,
                                             .motion = context->motion});
}

const ViewsModuleEntry VIEWS_MODULE_ENTRY = {
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_VIEWS, bind),
    .cancel = mixtape_ui_cancel,
    .step = mixtape_ui_step,
    .add = mixtape_ui_add,
    .section = browse_sections_at,
    .sections = browse_sections_range,
    .theme_boot = theme_runtime_boot,
    .theme_restore = theme_runtime_restore,
};
