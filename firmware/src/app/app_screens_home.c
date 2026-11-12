#include "app_screens.h"

#include <stddef.h>

static bool home_shown(const MenuItem *item)
{
    return item->shown == NULL || item->shown(item->shown_context);
}

static const void *home_selected(const AppScreens *screens)
{
    int remaining = screens->home.list.selected;
    for (int index = 0; index < screens->home.config.item_count; index++) {
        const MenuItem *item = &screens->home_items[index];
        if (home_shown(item) && remaining-- == 0) {
            return item->context;
        }
    }
    return NULL;
}

void app_screens_arrange_home(AppScreens *screens)
{
    const void *selected = home_selected(screens);
    screens->home.config.item_count = app_screens_arrange_home_items(screens);
    list_view_select(&screens->home.list, app_screens_home_position(screens, selected), 0);
}
