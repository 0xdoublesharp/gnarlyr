#include "debug_steps.h"
#include "screen_modules.h"

const DebugModuleEntry DEBUG_MODULE_ENTRY = {
    .magic = SCREEN_MODULE_MAGIC(DEBUG_MODULE_NUMBER),
    .step = debug_steps_step,
    .stop = debug_steps_stop,
    .frame = debug_steps_frame,
    .screenshot = debug_screenshot_save,
};
