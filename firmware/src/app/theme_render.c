#include "theme_runtime.h"

#include <stddef.h>

#include "hal/system.h"
#include "screen_modules.h"

const char THEME_SELECTION_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/theme.bin";

void theme_runtime_close(ThemeRuntime *runtime)
{
    if (runtime->active) {
        hal_file_close(&runtime->file);
    }
    runtime->active = false;
    runtime->tile_valid = false;
}

static bool draw_loaded(const Theme *theme, Canvas *canvas, int slot, Rect area)
{
    const ScreenModuleEntry *entry = hal_system_loaded_module();
    return entry != NULL && entry->theme_draw(theme, canvas, slot, area);
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
    theme->draw_art = runtime->active ? draw_loaded : NULL;
}
