#include "modules/modules.h"
#include "screens/charge_screen.h"

#include <stddef.h>

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    SettingsModuleScreens *own = &screens->modules.settings;
    SettingsContext settings = {
        .store = context->settings,
        .player = context->player,
        .power = context->power,
        .toast = context->toast,
        .rescan_requested = context->rescan_requested,
        .language_requested = context->language_requested,
        .bluetooth = &own->bluetooth,
        .theme = context->styled_theme,
        .sleep_option = 0,
    };
    bluetooth_screen_init(&own->bluetooth, context->bluetooth, context->theme);
    settings_screens_init(&own->settings, settings, context->theme);
    own->settings.items[SETTINGS_ABOUT_ITEM] =
        app_screens_open_item(STR_ABOUT, &own->diagnostics.screen);
    diagnostics_menu_build(screens, context->options, context->theme);
}

static Screen *language_row(AppScreens *screens)
{
    SettingsScreens *settings = &screens->modules.settings.settings;
    int row = 0;
    for (int index = 0; index < SETTINGS_LANGUAGE_ITEM; index++) {
        const MenuItem *item = &settings->items[index];
        row += item->shown == NULL || item->shown(item->shown_context) ? 1 : 0;
    }
    list_view_select(&settings->menu.list, row, 0);
    return &settings->menu.screen;
}

static Screen *usb_modes(AppScreens *screens)
{
    return settings_screens_usb_modes(&screens->modules.settings.settings);
}

#ifdef ECHO_DEBUG_LOOP
static bool choose(AppScreens *screens, DebugSpan title, DebugSpan option)
{
    return settings_screens_choose(&screens->modules.settings.settings, title, option);
}
#endif

const SettingsModuleEntry SETTINGS_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_SETTINGS), bind},
    .language_row = language_row,
    .usb_modes = usb_modes,
    .charge_draw = charge_art_draw,
#ifdef ECHO_DEBUG_LOOP
    .choose = choose,
#endif
};
