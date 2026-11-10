#include <string.h>

#include "app_screens.h"
#include "check.h"
#include "default_theme.h"

static AppScreens screens;
static AppOptions options;

static const MenuConfig *built(void)
{
    memset(&screens, 0, sizeof screens);
    diagnostics_menu_build(&screens, &options, &BOARD_THEME);
    return &screens.modules.settings.diagnostics.config;
}

static bool has_row(const MenuConfig *menu, const char *label)
{
    for (int index = 0; index < menu->item_count; index++) {
        if (strcmp(menu->items[index].label, label) == 0) {
            return true;
        }
    }
    return false;
}

static void the_release_menu_keeps_only_the_safe_rows(void)
{
    static const char *const SAFE[] = {"Firmware", "Battery",   "Jack 3.5", "Jack 4.4", "Music",
                                       "Headset",  "CPU clock", "DAC A",    "DAC B"};
    const MenuConfig *menu = built();
    CHECK_EQUAL(9, menu->item_count);
    for (size_t index = 0; index < sizeof SAFE / sizeof SAFE[0]; index++) {
        CHECK(has_row(menu, SAFE[index]));
    }
}

static void the_release_menu_has_no_tests_that_act_on_the_hardware(void)
{
    static const char *const DEVELOPER[] = {
        "Test tone",     "DMA tone", "Tone 192k",     "Tone 176k", "Stall test",
        "Watchdog test", "Core 1",   "Core 1 decode", "LPC accel", "High RAM",
        "Bluetooth",     "Profiler", "USB sense"};
    const MenuConfig *menu = built();
    for (size_t index = 0; index < sizeof DEVELOPER / sizeof DEVELOPER[0]; index++) {
        CHECK(!has_row(menu, DEVELOPER[index]));
    }
}

static void every_value_row_reads_without_a_selection(void)
{
    const MenuConfig *menu = built();
    for (int index = 0; index < menu->item_count; index++) {
        const MenuItem *item = &menu->items[index];
        if (!item->opens) {
            CHECK(item->select == NULL);
            CHECK(item->value != NULL);
            CHECK(item->value(item->context) != NULL);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_release_menu_keeps_only_the_safe_rows),
        TEST_CASE(the_release_menu_has_no_tests_that_act_on_the_hardware),
        TEST_CASE(every_value_row_reads_without_a_selection),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
