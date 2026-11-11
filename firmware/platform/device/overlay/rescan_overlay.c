#include "app/rescan_module.h"

const RescanModuleEntry *rescan_overlay_entry(void);

const RescanModuleEntry *rescan_overlay_entry(void)
{
    return &RESCAN_MODULE_ENTRY;
}
