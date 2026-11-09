#include <string.h>

#include "app_screens.h"
#include "check.h"
#include "default_theme.h"

static AppScreens screens;
static AppOptions options;

static void device_diagnostics_keep_hardware_controls_without_typography_samples(void)
{
    static const char *const CONTROLS[] = {"Key test",  "CPU clock",   "High RAM",  "Core 1",
                                           "Jack 3.5",  "Jack 4.4",    "Tone 192k", "Tone 176k",
                                           "Bluetooth", "USB HW2 HW3", "Last USB",  "Music"};
    diagnostics_menu_build(&screens, &options, &BOARD_THEME);
    const MenuConfig *menu = &screens.diagnostics.config;
    CHECK_EQUAL(DIAGNOSTICS_ITEM_COUNT - 3, menu->item_count);
    for (int index = 0; index < menu->item_count; index++) {
        const char *label = menu->items[index].label;
        CHECK(label != NULL);
        CHECK(strcmp(label, "Fonts") != 0);
        CHECK(strcmp(label, "Long names") != 0);
        CHECK(strcmp(label, "Loop cycles") != 0);
    }
    for (size_t control = 0; control < sizeof CONTROLS / sizeof CONTROLS[0]; control++) {
        bool found = false;
        for (int index = 0; index < menu->item_count; index++) {
            found |= strcmp(menu->items[index].label, CONTROLS[control]) == 0;
        }
        CHECK(found);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(device_diagnostics_keep_hardware_controls_without_typography_samples),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
