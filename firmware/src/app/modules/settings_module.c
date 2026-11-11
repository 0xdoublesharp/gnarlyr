#include "app/app.h"
#include "modules/modules.h"

#include <stddef.h>

#include "core/i18n.h"
#include "hal/clock.h"

static bool home_changed(void *context)
{
    AppScreens *screens = context;
    SettingsStore *store = screens->context.settings;
    settings_store_capture(store, player_status(screens->context.player));
    bool saved = settings_store_save(store);
    if (!saved) {
        toast_show(screens->context.toast, i18n_text(STR_SETTINGS_NOT_SAVED),
                   hal_clock_milliseconds());
    }
    app_screens_arrange_home(screens);
    return saved;
}

static void bind_home_editor(AppScreens *screens)
{
    SettingsModuleScreens *own = &screens->modules.settings;
    HomeEditorConfig config = {.settings = &screens->context.settings->settings,
                               .items = APP_HOME_ITEMS,
                               .theme = screens->context.theme,
                               .toast = screens->context.toast,
                               .changed = home_changed,
                               .context = screens};
    Screen *editor = home_editor_init(&own->home_editor, config);
    own->settings.items[SETTINGS_HOME_ITEM] = (MenuItem){.label = i18n_text(STR_HOME_SCREEN),
                                                         .select = menu_open_screen,
                                                         .context = editor,
                                                         .opens = true};
}

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    SettingsModuleScreens *own = &screens->modules.settings;
    SettingsContext settings = {
        .store = context->settings,
        .format_prepare = app_release_audio,
        .format_finished = app_storage_formatted,
        .headphones = context->headphones,
        .player = context->player,
        .power = context->power,
        .toast = context->toast,
        .rescan_requested = context->rescan_requested,
        .language_requested = context->language_requested,
        .bluetooth = &own->bluetooth,
        .theme = context->styled_theme,
    };
    bluetooth_screen_init(&own->bluetooth, context->bluetooth, context->theme);
    settings_screens_init(&own->settings, settings, context->theme);
    own->settings.items[SETTINGS_ABOUT_ITEM] =
        app_screens_open_item(STR_ABOUT, &screens->modules.views.diagnostics.screen);
    bind_home_editor(screens);
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

static Screen *usb_modes(AppScreens *screens, bool prompt)
{
    return settings_screens_usb_modes(&screens->modules.settings.settings, prompt);
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
#ifdef ECHO_DEBUG_LOOP
    .choose = choose,
#endif
    .dac_headphones = headphone_store_choose_for_dac,
};
