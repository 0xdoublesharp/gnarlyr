#include "app/app.h"
#include "modules/modules.h"
#include "settings_actions.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"

static bool home_changed(void *context)
{
    AppScreens *screens = context;
    SettingsStore *store = screens->context.settings;
    settings_capture(&store->settings, screens->context.player);
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
    own->settings.items[SETTINGS_HOME_ITEM].select = menu_open_screen;
    own->settings.items[SETTINGS_HOME_ITEM].context = editor;
}

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    SettingsModuleScreens *own = &screens->modules.settings;
    SettingsContext settings = {
        .store = context->settings,
        .equalizer = &screens->modules.equalizer.browser.choice.screen,
        .graphic = &screens->modules.equalizer.graphic.screen,
        .parametric = &screens->modules.equalizer.parametric.screen,
        .card_format = &screens->modules.format.card.screen,
        .format_prepare = app_release_audio,
        .reload = app_reload,
        .headphones = context->headphones,
        .player = context->player,
        .power = context->power,
        .toast = context->toast,
        .rescan_requested = context->rescan_requested,
        .language_requested = context->language_requested,
        .library = context->store,
        .bluetooth = &own->bluetooth,
        .theme = context->styled_theme,
    };
    bluetooth_screen_init(&own->bluetooth, context->bluetooth, context->theme);
    settings_screens_init(&own->settings, settings, context->theme);
    own->settings.about = &app_screens_about(screens)->diagnostics.screen;
    for (int at = 0; at < SETTINGS_ITEM_COUNT; at++) {
        if (own->settings.items[at].label == i18n_text(STR_THEME)) {
            own->settings.items[at] =
                app_screens_open_item(STR_THEME, &screens->modules.views.themes.screen);
        }
    }
    bind_home_editor(screens);
}

static Screen *language_row(AppScreens *screens)
{
    SettingsScreens *settings = &screens->modules.settings.settings;
    list_view_select(&settings->menu.list, SETTINGS_GROUP_LANGUAGE, 0);
    return &settings->menu.screen;
}

static Screen *usb_modes(AppScreens *screens, bool prompt)
{
    return settings_screens_usb_modes(&screens->modules.settings.settings, prompt);
}

static ScreenAction menu_hold(AppScreens *screens, uint8_t action)
{
    SettingsScreens *settings = &screens->modules.settings.settings;
    if (action == 4) {
        SettingsContext *context = &settings->context;
        int next =
            settings_action_current_shuffle(context) == SHUFFLE_OFF ? SHUFFLE_SONGS : SHUFFLE_OFF;
        settings_action_choose_shuffle(context, next);
        if (settings_action_current_shuffle(context) == next) {
            StringBuilder message = string_builder_make(context->message, sizeof context->message);
            string_builder_append(&message, i18n_text(STR_SHUFFLE));
            string_builder_append_char(&message, ':');
            string_builder_append_char(&message, ' ');
            string_builder_append(&message,
                                  i18n_text(next == SHUFFLE_OFF ? STR_OFF : STR_SHUFFLE_SONGS));
            toast_show(context->toast, message.buffer, hal_clock_milliseconds());
        }
        return screen_action_pop_to_root();
    }
    if (action == 5) {
        return settings_screens_sleep(settings);
    }
    Screen *screen = action == 2 ? &screens->modules.playing.up_next.screen
                                 : &screens->modules.equalizer.quick_settings.screen;
    return screen_action_push(screen);
}

#ifdef ECHO_DEVICE_OVERLAYS
static void reload(AppScreens *screens)
{
    settings_screens_restore_languages(&screens->modules.settings.settings);
}
#endif

const SettingsModuleEntry SETTINGS_MODULE_ENTRY = {
#ifdef ECHO_DEVICE_OVERLAYS
    .module = SCREEN_MODULE_RELOADING_ENTRY(SCREEN_MODULE_SETTINGS, bind, reload),
#else
    .module = SCREEN_MODULE_ENTRY(SCREEN_MODULE_SETTINGS, bind),
#endif
    .menu_hold = menu_hold,
    .language_row = language_row,
    .usb_modes = usb_modes,
#ifdef ECHO_DEBUG_LOOP
    .choose_tables = &SETTINGS_CHOOSE_TABLES,
#endif
    .dac_headphones = headphone_store_choose_for_dac,
};
