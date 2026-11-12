#include "screen_modules.h"

#include <stddef.h>

#include "app_screens.h"
#include "default_theme.h"
#include "hal/system.h"
#include "modules/modules.h"

typedef struct ModuleSpan {
    uint32_t offset;
    uint32_t size;
} ModuleSpan;

static const ModuleSpan SPANS[SCREEN_MODULE_COUNT] = {
    [SCREEN_MODULE_EQUALIZER] = {offsetof(AppScreens, modules.equalizer), sizeof(EqualizerUi)},
    [SCREEN_MODULE_LIBRARY] = {offsetof(AppScreens, modules.library), sizeof(LibraryModuleScreens)},
    [SCREEN_MODULE_LISTENING] = {offsetof(AppScreens, modules.listening),
                                 sizeof(ProfileModuleScreens)},
    [SCREEN_MODULE_VIEWS] = {offsetof(AppScreens, modules.views), sizeof(ViewsModuleScreens)},
    [SCREEN_MODULE_PLAYING] = {offsetof(AppScreens, modules.playing), sizeof(PlayingModuleScreens)},
    [SCREEN_MODULE_SETTINGS] = {offsetof(AppScreens, modules.settings),
                                sizeof(SettingsModuleScreens)},
};

ScreenModule screen_modules_of(const AppScreens *screens, const Screen *screen)
{
    if (screen == &screens->now_playing.screen && screens->now_playing.config.settings != NULL &&
        settings_now_playing_style(screens->now_playing.config.settings) != NOW_PLAYING_COVER) {
        return SCREEN_MODULE_PLAYING;
    }
    uintptr_t at = (uintptr_t)screen - (uintptr_t)screens;
    if (at - offsetof(AppScreens, browse) < sizeof(BrowsePool) ||
        screen == &screens->now_playing.screen) {
        return SCREEN_MODULE_PLAYING;
    }
    for (int module = SCREEN_MODULE_NONE + 1; module < SCREEN_MODULE_COUNT; module++) {
        if (at - SPANS[module].offset < SPANS[module].size) {
            return (ScreenModule)module;
        }
    }
    return SCREEN_MODULE_NONE;
}

static void cancel_settings_previews(AppScreens *screens)
{
    if ((screens->bound_modules & (1u << SCREEN_MODULE_SETTINGS)) != 0) {
        SettingsScreens *settings = &screens->modules.settings.settings;
        if (settings->preview.choice != STRING_COUNT) {
            settings->preview_settings(settings, false);
        }
    }
}

static void cancel_equalizer_previews(AppScreens *screens)
{
    if ((screens->bound_modules & (1u << SCREEN_MODULE_EQUALIZER)) == 0) {
        return;
    }
    EqualizerUi *ui = &screens->modules.equalizer;
    bool previewing = ui->browser.previewing || ui->graphic.previewing || ui->parametric.previewing;
    ui->browser.previewing = false;
    ui->graphic.previewing = false;
    ui->parametric.previewing = false;
    if (previewing) {
        settings_store_apply_equalizer(&screens->context.settings->settings);
    }
}

static void cancel_theme_preview(AppScreens *screens)
{
    if (screens->context.themes != NULL && screens->context.themes->previewing) {
        const ViewsModuleEntry *entry = hal_system_load_module(SCREEN_MODULE_VIEWS);
        if (entry != NULL) {
            entry->theme_restore(screens->context.themes);
            default_theme_invalidate(screens->context.styled_theme);
        }
    }
}

void screen_modules_cancel_previews(AppScreens *screens)
{
    cancel_theme_preview(screens);
    cancel_settings_previews(screens);
    cancel_equalizer_previews(screens);
}

const ScreenModuleEntry *screen_modules_use(AppScreens *screens, ScreenModule module)
{
    if (screens->context.themes == NULL || !screens->context.themes->previewing) {
        if (module != SCREEN_MODULE_SETTINGS) {
            cancel_settings_previews(screens);
        }
        if (module != SCREEN_MODULE_EQUALIZER) {
            cancel_equalizer_previews(screens);
        }
    }
    const ScreenModuleEntry *entry = hal_system_load_module((uint32_t)module);
    if (entry == NULL || entry->magic != SCREEN_MODULE_MAGIC(module)) {
        screens->home.config.panel.update = NULL;
        screens->home.config.panel.draw = NULL;
        screens->home_art.unavailable = true;
        return NULL;
    }
    uint32_t bound = 1u << module;
    if (entry->use_language((uint32_t)module)) {
        screens->bound_modules &= ~bound;
    }
    if ((screens->bound_modules & bound) == 0) {
        screens->bound_modules |= bound;
        entry->bind(screens);
    }
    screens->home_art.module = (uint8_t)module;
    screens->home_art.unavailable = false;
    screens->home.config.panel.update = entry->art_update;
    screens->home.config.panel.draw = entry->art_draw;
    return entry;
}

void screen_modules_prepare_home(AppScreens *screens)
{
    HomeArt *art = &screens->home_art;
    if (!art->unavailable) {
        ScreenModule module = art->module != 0 ? (ScreenModule)art->module : SCREEN_MODULE_PLAYING;
        art->unavailable = screen_modules_use(screens, module) == NULL;
    }
}
