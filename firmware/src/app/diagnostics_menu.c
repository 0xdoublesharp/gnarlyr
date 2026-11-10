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
    return i18n_text(options->show_frame_time ? STR_ON : STR_OFF);
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
    return i18n_text(options->ignore_power_taps ? STR_ABOUT_TAPS_IGNORED : STR_ABOUT_TAPS_NORMAL);
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
    static const StringId STATES[] = {[HAL_PROFILE_OFF] = STR_OFF,
                                      [HAL_PROFILE_ON] = STR_ON,
                                      [HAL_PROFILE_FULL] = STR_ABOUT_PROFILE_FULL};
    (void)context;
    return i18n_text(STATES[hal_system_profile_state()]);
}

static char usb_sense_text[DIAGNOSTICS_VALUE_CAPACITY];

static ScreenAction switch_usb_sense(void *context)
{
    (void)context;
    hal_system_keep_usb_on(!hal_system_usb_sense().kept_on);
    return screen_action_none();
}

static const char *usb_sense_value(const void *context)
{
    (void)context;
    diagnostics_usb_sense_text(hal_system_usb_sense(), usb_sense_text, sizeof usb_sense_text);
    return usb_sense_text;
}

static ScreenAction switch_usb_connect(void *context)
{
    (void)context;
    hal_system_usb_connect(!hal_system_usb_sense().connecting);
    return screen_action_none();
}

static const char *usb_connect_value(const void *context)
{
    (void)context;
    return i18n_text(hal_system_usb_sense().connecting ? STR_ABOUT_LISTENING : STR_OFF);
}

typedef enum DiagnosticsContext {
    CONTEXT_NONE,
    CONTEXT_OPTIONS,
    CONTEXT_CLOCK,
    CONTEXT_BLUETOOTH,
    CONTEXT_COUNT
} DiagnosticsContext;

typedef struct DiagnosticsRow {
    StringId label;
    ScreenAction (*select)(void *context);
    const char *(*value)(const void *context);
    uint8_t context;
    bool opens;
} DiagnosticsRow;

static const DiagnosticsRow ROWS[DIAGNOSTICS_ITEM_COUNT] = {
    [0] = {STR_ABOUT_FRAME_TIME, toggle_frame_time, frame_time_value, CONTEXT_OPTIONS, false},
    [1] = {STR_ABOUT_FIRMWARE, NULL, diagnostics_row_firmware, CONTEXT_NONE, false},
    [2] = {STR_ABOUT_CPU_CLOCK, cycle_clock, clock_value, CONTEXT_CLOCK, false},
    [3] = {STR_ABOUT_BATTERY, NULL, diagnostics_row_battery, CONTEXT_NONE, false},
    [4] = {STR_BLUETOOTH, bluetooth_screen_open_details, NULL, CONTEXT_BLUETOOTH, true},
    [5] = {STR_ABOUT_STALL_TEST, run_stall_test, NULL, CONTEXT_NONE, false},
    [6] = {STR_ABOUT_POWER_TAPS, toggle_power_taps, power_taps_value, CONTEXT_OPTIONS, false},
    [7] = {STR_ABOUT_JACK_35, NULL, diagnostics_row_jack_single_ended, CONTEXT_NONE, false},
    [8] = {STR_ABOUT_JACK_44, NULL, diagnostics_row_jack_balanced, CONTEXT_NONE, false},
    [9] = {STR_ABOUT_WATCHDOG_TEST, run_watchdog_test, NULL, CONTEXT_NONE, false},
    [10] = {STR_MUSIC, NULL, diagnostics_row_music, CONTEXT_NONE, false},
    [11] = {STR_ABOUT_HEADSET, NULL, diagnostics_row_headset, CONTEXT_NONE, false},
    [12] = {STR_ABOUT_CORE_BUSY, NULL, diagnostics_row_core0_busy, CONTEXT_NONE, false},
    [13] = {STR_ABOUT_DAC_A, NULL, diagnostics_row_dac_a, CONTEXT_NONE, false},
    [14] = {STR_ABOUT_DAC_B, NULL, diagnostics_row_dac_b, CONTEXT_NONE, false},
    [15] = {STR_ABOUT_PROFILER, switch_profiler, profiler_value, CONTEXT_NONE, false},
    [16] = {STR_ABOUT_USB_SENSE, switch_usb_sense, usb_sense_value, CONTEXT_NONE, false},
    [17] = {STR_ABOUT_CONNECT_USB, switch_usb_connect, usb_connect_value, CONTEXT_NONE, false},
};

static void options_init(AppScreens *screens)
{
    screens->modules.settings.clock = (ClockOption){.failed = false};
}

static void build_rows(SettingsModuleScreens *screens, AppOptions *options)
{
    void *contexts[CONTEXT_COUNT] = {[CONTEXT_OPTIONS] = options,
                                     [CONTEXT_CLOCK] = &screens->clock,
                                     [CONTEXT_BLUETOOTH] = &screens->bluetooth};
    for (int index = 0; index < DIAGNOSTICS_ITEM_COUNT; index++) {
        const DiagnosticsRow *row = &ROWS[index];
        screens->diagnostics_items[index] = (MenuItem){.label = i18n_text(row->label),
                                                       .select = row->select,
                                                       .context = contexts[row->context],
                                                       .value = row->value,
                                                       .opens = row->opens};
    }
}

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    SettingsModuleScreens *own = &screens->modules.settings;
    options_init(screens);
    build_rows(own, options);
    app_screens_build_menu(&own->diagnostics, STR_ABOUT, own->diagnostics_items,
                           DIAGNOSTICS_ITEM_COUNT, theme);
}
