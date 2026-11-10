#include "diagnostics_rows.h"

#include "core/i18n.h"
#include "diagnostics_values.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/system.h"

const char *diagnostics_row_firmware(const void *context)
{
    (void)context;
    return hal_system_version();
}

const char *diagnostics_row_music(const void *context)
{
    (void)context;
    switch (hal_file_volume()) {
    case HAL_VOLUME_CARD:
        return i18n_text(STR_ABOUT_CARD);
    case HAL_VOLUME_INTERNAL:
        return i18n_text(STR_ABOUT_INTERNAL);
    case HAL_VOLUME_NONE:
        break;
    }
    return i18n_text(STR_ABOUT_NONE);
}

static char battery_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_battery(const void *context)
{
    (void)context;
    diagnostics_battery_text(hal_power_battery(), battery_text, sizeof battery_text);
    return battery_text;
}

static char jack_single_ended_text[DIAGNOSTICS_VALUE_CAPACITY];
static char jack_balanced_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_jack_single_ended(const void *context)
{
    (void)context;
    diagnostics_jack_pin_text(hal_audio_jack_pins().single_ended, jack_single_ended_text,
                              sizeof jack_single_ended_text);
    return jack_single_ended_text;
}

const char *diagnostics_row_jack_balanced(const void *context)
{
    (void)context;
    diagnostics_jack_pin_text(hal_audio_jack_pins().balanced, jack_balanced_text,
                              sizeof jack_balanced_text);
    return jack_balanced_text;
}

static char headset_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_headset(const void *context)
{
    (void)context;
    diagnostics_headset_text(hal_input_headset_reading(), headset_text, sizeof headset_text);
    return headset_text;
}

static char cpu_text[DIAGNOSTICS_VALUE_CAPACITY];
static LoadWindow load_window;

const char *diagnostics_row_cpu_clock(const void *context)
{
    (void)context;
    diagnostics_load_update(&load_window, hal_audio_stats());
    diagnostics_cpu_text(hal_system_clock_mhz(), &load_window, cpu_text, sizeof cpu_text);
    return cpu_text;
}

static char busy_text[DIAGNOSTICS_VALUE_CAPACITY];
static LoadWindow busy_window;

const char *diagnostics_row_core0_busy(const void *context)
{
    (void)context;
    uint32_t elapsed_us = hal_clock_microseconds();
    HalAudioStats stats = {.busy_us = elapsed_us - hal_system_idle_microseconds(),
                           .elapsed_us = elapsed_us};
    diagnostics_load_update(&busy_window, stats);
    diagnostics_busy_text(&busy_window, busy_text, sizeof busy_text);
    return busy_text;
}
