#include "theme_runtime.h"

#include <stddef.h>

const char THEME_SELECTION_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/theme.bin";

void theme_runtime_close(ThemeRuntime *runtime)
{
    if (runtime->active) {
        hal_file_close(&runtime->file);
    }
    runtime->active = false;
    runtime->tile_valid = false;
}

void theme_runtime_apply(ThemeRuntime *runtime, Theme *theme)
{
    if (runtime->active) {
        theme->appearance = UINT8_MAX;
        theme->colors = runtime->pack.palette.colors;
        theme->horizon = runtime->pack.palette.horizon;
        theme->header_rule = runtime->pack.palette.header_rule;
    }
    theme->art_context = runtime;
    theme->draw_art = runtime->active ? theme_art_module_draw : NULL;
}
