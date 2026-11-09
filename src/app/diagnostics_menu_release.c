#include "app_screens.h"

#include <stddef.h>

#include "diagnostics_rows.h"

enum {
    RELEASE_DIAGNOSTICS_COUNT = 7
};

static MenuItem value_item(const char *label, const char *(*value)(const void *context))
{
    return (MenuItem){.label = label, .select = NULL, .context = NULL, .value = value};
}

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    (void)options;
    MenuItem *items = screens->diagnostics_items;
    items[0] = value_item("Firmware", diagnostics_row_firmware);
    items[1] = value_item("Battery", diagnostics_row_battery);
    items[2] = value_item("Jack 3.5", diagnostics_row_jack_single_ended);
    items[3] = value_item("Jack 4.4", diagnostics_row_jack_balanced);
    items[4] = value_item("Music", diagnostics_row_music);
    items[5] = value_item("Headset", diagnostics_row_headset);
    items[6] = value_item("CPU clock", diagnostics_row_cpu_clock);
    app_screens_build_menu(&screens->diagnostics, "About", items, RELEASE_DIAGNOSTICS_COUNT, theme);
}
