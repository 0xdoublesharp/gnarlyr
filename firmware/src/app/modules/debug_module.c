#include "debug_runner.h"
#include "debug_steps.h"
#include "settings_choose.h"

const DebugCodeEntry DEBUG_CODE_ENTRY = {
    .step = debug_steps_step,
    .stop = debug_steps_stop,
    .frame = debug_steps_frame,
    .screenshot = debug_screenshot_save,
    .choose_setting = settings_choose,
};
