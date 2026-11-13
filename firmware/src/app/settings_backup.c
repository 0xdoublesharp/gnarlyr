#include "settings_screens.h"

#include "core/i18n.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "settings/user_state.h"

static void tell(const SettingsScreens *screens, StringId text)
{
    toast_show(screens->context.toast, i18n_text(text), hal_clock_milliseconds());
}

static ScreenAction restore(SettingsScreens *screens, int position)
{
    int slot = user_state_list_order(&screens->backups, position);
    uint8_t mark[USER_STATE_MARK_BYTES];
    bool read = hal_internal_file_read(USER_STATE_MARK_PATH, mark, sizeof mark);
    mark[USER_STATE_MARK_REQUEST_AT] = (uint8_t)(slot + 1);
    if (slot < 0 || !read || !hal_internal_file_write(USER_STATE_MARK_PATH, mark, sizeof mark)) {
        tell(screens, STR_RESTORE_FAILED);
        return screen_action_none();
    }
    if (screens->context.format_prepare != NULL) {
        screens->context.format_prepare();
    }
    if (screens->context.reload != NULL) {
        screens->context.reload(STR_SETTINGS_RESTORED);
    }
    return screen_action_pop_to_root();
}

static ScreenAction restore_newest(void *context)
{
    return restore(context, 0);
}

static ScreenAction restore_older(void *context)
{
    return restore(context, 1);
}

static void read_backups(SettingsScreens *screens)
{
    uint8_t bytes[USER_STATE_LIST_BYTES] = {0};
    (void)hal_internal_file_read(USER_STATE_LIST_PATH, bytes, sizeof bytes);
    screens->backups = user_state_list_decode(bytes);
}

static ScreenAction open_backups(void *context)
{
    static ScreenAction (*const RESTORE[USER_STATE_BACKUPS])(void *) = {restore_newest,
                                                                        restore_older};
    SettingsScreens *screens = context;
    read_backups(screens);
    int count = 0;
    for (int slot; (slot = user_state_list_order(&screens->backups, count)) >= 0; count++) {
        screens->backup_items[count] = (MenuItem){
            .label = screens->backups.builds[slot], .select = RESTORE[count], .context = screens};
    }
    MenuConfig config = {.title = i18n_text(STR_RESTORE_BACKUP),
                         .items = screens->backup_items,
                         .item_count = count,
                         .theme = screens->theme,
                         .detail_below = true};
    return screen_action_push(menu_screen_init(&screens->backup_menu, config));
}

static bool backups_shown(const void *context)
{
    return user_state_list_order(&((const SettingsScreens *)context)->backups, 0) >= 0;
}

MenuItem settings_backup_item(SettingsScreens *screens)
{
    read_backups(screens);
    return (MenuItem){.label = i18n_text(STR_BACKUPS),
                      .select = open_backups,
                      .context = screens,
                      .opens = true,
                      .shown = backups_shown,
                      .shown_context = screens};
}
