#include "debug_runner.h"
#include "extension_entry.h"
#include "hal/storage.h"
#include "screen_modules.h"

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
    .art_init = home_art_init,
    .art_update = home_art_update,
    .art_draw = home_art_draw,
    .art_backdrop = home_art_draw_backdrop,
    .file_remove = hal_file_remove,
};
