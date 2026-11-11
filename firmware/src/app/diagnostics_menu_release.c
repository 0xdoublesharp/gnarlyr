#include "app_screens.h"

#include <stddef.h>

#include "diagnostics_rows.h"

enum {
    RELEASE_DIAGNOSTICS_COUNT = 7
};

static MenuItem value_item(StringId label, const char *(*value)(const void *context))
{
    return (MenuItem){.label = i18n_text(label), .select = NULL, .context = NULL, .value = value};
}

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    (void)options;
    MenuItem *items = screens->modules.utilities.diagnostics_items;
    items[0] = value_item(STR_ABOUT_FIRMWARE, diagnostics_row_firmware);
    items[1] = value_item(STR_ABOUT_BATTERY, diagnostics_row_battery);
    items[2] = value_item(STR_ABOUT_JACK_35, diagnostics_row_jack_single_ended);
    items[3] = value_item(STR_ABOUT_JACK_44, diagnostics_row_jack_balanced);
    items[4] = value_item(STR_MUSIC, diagnostics_row_music);
    items[5] = value_item(STR_ABOUT_HEADSET, diagnostics_row_headset);
    items[6] = value_item(STR_ABOUT_CPU_CLOCK, diagnostics_row_cpu_clock);
    app_screens_build_menu(&screens->modules.utilities.diagnostics, STR_ABOUT, items,
                           RELEASE_DIAGNOSTICS_COUNT, theme);
}
