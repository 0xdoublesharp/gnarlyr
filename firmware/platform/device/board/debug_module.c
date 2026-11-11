#include <stdbool.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "hal/system.h"
#include "overlay.h"

static bool ready;

const void *hal_system_load_debug_code(void)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = MODULE_MAGIC | DEBUG_MODULE_NUMBER << MODULE_MAGIC_SHIFT,
                                  .base = (uint8_t *)(uintptr_t)DEBUG_MODULE_BASE,
                                  .capacity = DEBUG_MODULE_BYTES,
                                  .module = DEBUG_MODULE_NUMBER};
    const uint32_t *entry = (const uint32_t *)(uintptr_t)(DEBUG_MODULE_BASE + OVERLAY_HEADER_BYTES);
    if (!ready) {
        ready = board_overlay_load(&overlay) && *entry == overlay.magic;
    }
    return ready ? entry : NULL;
}
