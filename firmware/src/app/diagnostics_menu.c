#include "app_screens.h"

#include <stdbool.h>
#include <stddef.h>

#include "diagnostics_rows.h"
#include "diagnostics_values.h"
#include "hal/power.h"
#include "hal/system.h"

static ScreenAction toggle_frame_time(void *context)
{
    AppOptions *options = context;
    options->show_frame_time = !options->show_frame_time;
    return screen_action_none();
}

static const char *frame_time_value(const void *context)
{
    const AppOptions *options = context;
    return options->show_frame_time ? "On" : "Off";
}

static ScreenAction toggle_power_taps(void *context)
{
    AppOptions *options = context;
    options->ignore_power_taps = !options->ignore_power_taps;
    return screen_action_none();
}

static const char *power_taps_value(const void *context)
{
    const AppOptions *options = context;
    return options->ignore_power_taps ? "Ignored" : "Normal";
}

static char clock_text[DIAGNOSTICS_VALUE_CAPACITY];

static ScreenAction cycle_clock(void *context)
{
    ClockOption *clock = context;
    clock->failed = !hal_system_set_clock_mhz(diagnostics_next_clock_mhz(hal_system_clock_mhz()));
    return screen_action_none();
}

static const char *clock_value(const void *context)
{
    const ClockOption *clock = context;
    if (!clock->failed) {
        return diagnostics_row_cpu_clock(NULL);
    }
    diagnostics_clock_text(hal_system_clock_mhz(), true, clock_text, sizeof clock_text);
    return clock_text;
}

static ScreenAction run_stall_test(void *context)
{
    (void)context;
    hal_system_stall_test();
    return screen_action_none();
}

static ScreenAction run_watchdog_test(void *context)
{
    (void)context;
    hal_system_watchdog_test();
    return screen_action_none();
}

static ScreenAction switch_profiler(void *context)
{
    (void)context;
    switch (hal_system_profile_state()) {
    case HAL_PROFILE_OFF:
        hal_system_set_profiling(true);
        break;
    case HAL_PROFILE_ON:
        hal_system_set_profiling(false);
        break;
    case HAL_PROFILE_FULL:
        hal_system_reset_profile();
        break;
    }
    return screen_action_none();
}

static const char *profiler_value(const void *context)
{
    static const char *const STATES[] = {
        [HAL_PROFILE_OFF] = "Off", [HAL_PROFILE_ON] = "On", [HAL_PROFILE_FULL] = "Full"};
    (void)context;
    return STATES[hal_system_profile_state()];
}

typedef enum DiagnosticsContext {
    CONTEXT_NONE,
    CONTEXT_OPTIONS,
    CONTEXT_CLOCK,
    CONTEXT_BLUETOOTH,
    CONTEXT_COUNT
} DiagnosticsContext;

typedef struct DiagnosticsRow {
    const char *label;
    ScreenAction (*select)(void *context);
    const char *(*value)(const void *context);
    uint8_t context;
    bool opens;
} DiagnosticsRow;

static const DiagnosticsRow ROWS[DIAGNOSTICS_ITEM_COUNT] = {
    [0] = {"Frame time", toggle_frame_time, frame_time_value, CONTEXT_OPTIONS, false},
    [1] = {"Firmware", NULL, diagnostics_row_firmware, CONTEXT_NONE, false},
    [2] = {"CPU clock", cycle_clock, clock_value, CONTEXT_CLOCK, false},
    [3] = {"Battery", NULL, diagnostics_row_battery, CONTEXT_NONE, false},
    [4] = {"Bluetooth", bluetooth_screen_open_details, NULL, CONTEXT_BLUETOOTH, true},
    [5] = {"Stall test", run_stall_test, NULL, CONTEXT_NONE, false},
    [6] = {"Power taps", toggle_power_taps, power_taps_value, CONTEXT_OPTIONS, false},
    [7] = {"Jack 3.5", NULL, diagnostics_row_jack_single_ended, CONTEXT_NONE, false},
    [8] = {"Jack 4.4", NULL, diagnostics_row_jack_balanced, CONTEXT_NONE, false},
    [9] = {"Watchdog test", run_watchdog_test, NULL, CONTEXT_NONE, false},
    [10] = {"Music", NULL, diagnostics_row_music, CONTEXT_NONE, false},
    [11] = {"Headset", NULL, diagnostics_row_headset, CONTEXT_NONE, false},
    [12] = {"Core 0 busy", NULL, diagnostics_row_core0_busy, CONTEXT_NONE, false},
    [13] = {"DAC A", NULL, diagnostics_row_dac_a, CONTEXT_NONE, false},
    [14] = {"DAC B", NULL, diagnostics_row_dac_b, CONTEXT_NONE, false},
    [15] = {"Profiler", switch_profiler, profiler_value, CONTEXT_NONE, false},
};

static void options_init(AppScreens *screens)
{
    screens->clock = (ClockOption){.failed = false};
}

static void build_rows(AppScreens *screens, AppOptions *options)
{
    void *contexts[CONTEXT_COUNT] = {[CONTEXT_OPTIONS] = options,
                                     [CONTEXT_CLOCK] = &screens->clock,
                                     [CONTEXT_BLUETOOTH] = &screens->bluetooth};
    for (int index = 0; index < DIAGNOSTICS_ITEM_COUNT; index++) {
        const DiagnosticsRow *row = &ROWS[index];
        if (row->label != NULL) {
            screens->diagnostics_items[index] = (MenuItem){.label = row->label,
                                                           .select = row->select,
                                                           .context = contexts[row->context],
                                                           .value = row->value,
                                                           .opens = row->opens};
        }
    }
}

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    options_init(screens);
    build_rows(screens, options);
    app_screens_build_menu(&screens->diagnostics, "About", screens->diagnostics_items,
                           DIAGNOSTICS_ITEM_COUNT, theme);
}
