#include <stddef.h>

#include "extension_entry.h"

bool hal_clock_set_wall(HalWallTime time)
{
    const ExtensionModuleEntry *code = hal_system_extension_code();
    return code != NULL && code->clock_set(time);
}
