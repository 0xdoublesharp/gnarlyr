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

static ScreenAction keep_running(void *context)
{
    (void)context;
    return screen_action_pop();
}

static ScreenAction freeze_player(void *context)
{
    (void)context;
    hal_system_stall_test();
    return screen_action_pop();
}

static void build_stall_question(ViewsModuleScreens *own, const Theme *theme)
{
    own->stall_items[0] = (MenuItem){.label = i18n_text(STR_CANCEL), .select = keep_running};
    own->stall_items[1] =
        (MenuItem){.label = i18n_text(STR_ABOUT_STALL_FREEZE), .select = freeze_player};
    app_screens_build_menu(&own->stall_question, STR_ABOUT_STALL_QUESTION, own->stall_items, 2,
                           theme);
}

static ScreenAction ask_stall_test(void *context)
{
    AppScreens *screens = context;
    build_stall_question(&screens->modules.views, screens->context.theme);
    return screen_action_push(&screens->modules.views.stall_question.screen);
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

typedef enum DiagnosticsContext {
    CONTEXT_NONE,
    CONTEXT_OPTIONS,
    CONTEXT_CLOCK,
    CONTEXT_BLUETOOTH,
    CONTEXT_KEY_TEST,
    CONTEXT_SCREENS,
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
    [5] = {STR_ABOUT_STALL_TEST, ask_stall_test, NULL, CONTEXT_SCREENS, true},
    [6] = {STR_ABOUT_POWER_TAPS, toggle_power_taps, power_taps_value, CONTEXT_OPTIONS, false},
    [7] = {STR_ABOUT_JACK_35, NULL, diagnostics_row_jack_single_ended, CONTEXT_NONE, false},
    [8] = {STR_ABOUT_JACK_44, NULL, diagnostics_row_jack_balanced, CONTEXT_NONE, false},
    [9] = {STR_ABOUT_WATCHDOG_TEST, run_watchdog_test, NULL, CONTEXT_NONE, false},
    [10] = {STR_MUSIC, NULL, diagnostics_row_music, CONTEXT_NONE, false},
    [11] = {STR_ABOUT_HEADSET, NULL, diagnostics_row_headset, CONTEXT_NONE, false},
    [12] = {STR_ABOUT_CORE_BUSY, NULL, diagnostics_row_core0_busy, CONTEXT_NONE, false},
    [14] = {STR_ABOUT_KEYS, menu_open_screen, diagnostics_row_keys, CONTEXT_KEY_TEST, true},
    [13] = {STR_ABOUT_PROFILER, switch_profiler, profiler_value, CONTEXT_NONE, false},
};

static void options_init(AppScreens *screens)
{
    screens->modules.views.diagnostic_clock = (ClockOption){.failed = false};
}

static void build_rows(AppScreens *screens, AppOptions *options)
{
    void *contexts[CONTEXT_COUNT] = {[CONTEXT_OPTIONS] = options,
                                     [CONTEXT_CLOCK] = &screens->modules.views.diagnostic_clock,
                                     [CONTEXT_BLUETOOTH] = &screens->modules.settings.bluetooth,
                                     [CONTEXT_KEY_TEST] =
                                         &screens->modules.equalizer.key_test.screen,
                                     [CONTEXT_SCREENS] = screens};
    for (int index = 0; index < DIAGNOSTICS_ITEM_COUNT; index++) {
        const DiagnosticsRow *row = &ROWS[index];
        screens->modules.views.diagnostics_items[index] =
            (MenuItem){.label = i18n_text(row->label),
                       .select = row->select,
                       .context = contexts[row->context],
                       .value = row->value,
                       .opens = row->opens};
    }
}

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    ViewsModuleScreens *own = &screens->modules.views;
    options_init(screens);
    build_rows(screens, options);
    app_screens_build_menu(&own->diagnostics, STR_ABOUT, own->diagnostics_items,
                           DIAGNOSTICS_ITEM_COUNT, theme);
}
