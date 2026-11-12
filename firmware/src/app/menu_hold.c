#include "extension_entry.h"
#include "modules/modules.h"

#include <stddef.h>

bool app_menu_hold(AppScreens *screens, KeyEvent event)
{
    uint8_t action = screens->context.settings->settings.long_press_action;
    if (action == 0 || action > 5 || event.key != KEY_MENU ||
        event.gesture != KEY_GESTURE_LONG_PRESS) {
        return false;
    }
    if (action == 3) {
        const ViewsModuleEntry *module =
            (const ViewsModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_VIEWS);
        Player *player = screens->context.player;
        if (module == NULL || !player->has_track) {
            return false;
        }
        screens->shortcut_destination =
            module->add(&screens->modules.views.mixtapes, player->track);
        return true;
    }
    const SettingsModuleEntry *module =
        (const SettingsModuleEntry *)screen_modules_use(screens, SCREEN_MODULE_SETTINGS);
    if (module == NULL) {
        return false;
    }
    screens->shortcut_destination = module->menu_hold(screens, action).target;
    return true;
}
