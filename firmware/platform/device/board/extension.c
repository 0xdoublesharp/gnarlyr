#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/extension.h"
#include "audio_effects.h"
#include "board.h"
#include "hal/system.h"
#include "overlay.h"

static const ExtensionModuleHead *extension;

void board_extension_start(void)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = MODULE_MAGIC | EXTENSION_MODULE_NUMBER
                                                              << MODULE_MAGIC_SHIFT,
                                  .base = (uint8_t *)(uintptr_t)EXTENSION_MODULE_BASE,
                                  .capacity = EXTENSION_MODULE_BYTES,
                                  .module = EXTENSION_MODULE_NUMBER};
    const ExtensionModuleHead *head =
        (const ExtensionModuleHead *)(uintptr_t)(EXTENSION_MODULE_BASE + OVERLAY_HEADER_BYTES);
    extension = board_overlay_load(&overlay) && head->magic == overlay.magic ? head : NULL;
}

const void *hal_system_extension_code(void)
{
    return extension;
}

#ifdef ECHO_DEBUG_LOOP
const void *hal_system_load_debug_code(void)
{
    return extension != NULL ? board_audio_effects_debug_code() : NULL;
}
#endif
