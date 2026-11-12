#include "debug_runner.h"
#include "extension_entry.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "screen_modules.h"

#ifdef ECHO_DEVICE_MODULE
#include "board.h"
#endif

const ExtensionModuleEntry EXTENSION_MODULE_ENTRY = {
    .head = {.magic = SCREEN_MODULE_MAGIC(EXTENSION_MODULE_NUMBER)},
    .motion_run = wire_motion_run,
    .motion_progress = wire_motion_progress,
    .motion_end = wire_motion_end,
    .motion_shown = wire_motion_shown,
    .motion_due = wire_motion_due,
    .motion_tick = wire_motion_tick,
    .motion_fit = wire_motion_fit,
    .motion_draw = wire_motion_draw,
    .file_remove = hal_file_remove,
    .headphones_load = headphone_store_load,
    .clock_set = hal_clock_set_wall,
    .clock_restore = hal_clock_restore_wall,
#ifdef ECHO_DEVICE_MODULE
    .forget_time_file = board_wall_time_delete_file,
#endif
};
