#include "screen_modules.h"

#include <stddef.h>

#include "app_screens.h"
#include "hal/system.h"

typedef struct ModuleSpan {
    uint32_t offset;
    uint32_t size;
} ModuleSpan;

static const ModuleSpan SPANS[SCREEN_MODULE_COUNT] = {
    [SCREEN_MODULE_LISTENING] = {offsetof(AppScreens, modules.listening),
                                 sizeof(ProfileModuleScreens)},
    [SCREEN_MODULE_VIEWS] = {offsetof(AppScreens, modules.views), sizeof(ViewsModuleScreens)},
    [SCREEN_MODULE_SETTINGS] = {offsetof(AppScreens, modules.settings),
                                sizeof(SettingsModuleScreens)},
};

ScreenModule screen_modules_of(const AppScreens *screens, const Screen *screen)
{
    uintptr_t at = (uintptr_t)screen - (uintptr_t)screens;
    for (int module = SCREEN_MODULE_NONE + 1; module < SCREEN_MODULE_COUNT; module++) {
        if (at - SPANS[module].offset < SPANS[module].size) {
            return (ScreenModule)module;
        }
    }
    return SCREEN_MODULE_NONE;
}

const ScreenModuleEntry *screen_modules_use(AppScreens *screens, ScreenModule module)
{
    const ScreenModuleEntry *entry = hal_system_load_module((uint32_t)module);
    if (entry == NULL || entry->magic != SCREEN_MODULE_MAGIC(module)) {
        return NULL;
    }
    uint32_t bound = 1u << module;
    if ((screens->bound_modules & bound) == 0) {
        screens->bound_modules |= bound;
        entry->bind(screens);
    }
    return entry;
}
