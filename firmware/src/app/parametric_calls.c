#include "parametric_store.h"

#include <stddef.h>

#include "hal/system.h"

void parametric_store_load(ParametricStore *store, ParametricEq *custom)
{
    *store = (ParametricStore){0};
    const ParametricModuleEntry *entry = hal_system_load_module(SCREEN_MODULE_EQUALIZER);
    if (entry != NULL && entry->module.magic == SCREEN_MODULE_MAGIC(SCREEN_MODULE_EQUALIZER)) {
        entry->load(store, custom);
    } else {
        *store = (ParametricStore){0};
        store->file.needs_reload = true;
        *custom = (ParametricEq){0};
    }
}

int parametric_store_matching(const Settings *settings)
{
    const ParametricModuleEntry *entry = hal_system_load_module(SCREEN_MODULE_EQUALIZER);
    return entry != NULL && entry->module.magic == SCREEN_MODULE_MAGIC(SCREEN_MODULE_EQUALIZER)
               ? entry->matching(settings)
               : -1;
}
