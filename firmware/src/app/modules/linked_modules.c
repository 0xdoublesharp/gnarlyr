#include "app/linked_modules.h"

#include <stddef.h>

#include "debug_runner.h"
#include "modules/modules.h"

const void *app_linked_module(uint32_t module)
{
    static const void *const ENTRIES[SCREEN_MODULE_COUNT] = {
        [SCREEN_MODULE_VIEWS] = &VIEWS_MODULE_ENTRY,
        [SCREEN_MODULE_SETTINGS] = &SETTINGS_MODULE_ENTRY,
        [SCREEN_MODULE_LIBRARY] = &LIBRARY_MODULE_ENTRY,
        [SCREEN_MODULE_LISTENING] = &LISTENING_MODULE_ENTRY,
    };
    return module < SCREEN_MODULE_COUNT ? ENTRIES[module] : NULL;
}

const void *app_linked_debug_code(void)
{
#ifdef ECHO_DEBUG_LOOP
    return &DEBUG_CODE_ENTRY;
#else
    return NULL;
#endif
}
