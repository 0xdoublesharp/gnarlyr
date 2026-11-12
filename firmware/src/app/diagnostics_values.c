#include "diagnostics_values.h"

#include "core/i18n.h"
#include "core/string_builder.h"

static const uint32_t CLOCK_STEPS_MHZ[] = {24, 96, 200};

uint32_t diagnostics_next_clock_mhz(uint32_t current_mhz)
{
    for (size_t index = 0; index < sizeof CLOCK_STEPS_MHZ / sizeof CLOCK_STEPS_MHZ[0]; index++) {
        if (CLOCK_STEPS_MHZ[index] > current_mhz) {
            return CLOCK_STEPS_MHZ[index];
        }
    }
    return CLOCK_STEPS_MHZ[0];
}

void diagnostics_clock_text(uint32_t mhz, bool failed, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (failed) {
        string_builder_append(&builder, "No lock");
        return;
    }
    string_builder_append_unsigned(&builder, mhz);
    string_builder_append(&builder, " MHz");
}

enum {
    LOAD_WINDOW_US = 1000000,
    LOAD_PERCENT = 100
};

void diagnostics_load_update(LoadWindow *window, HalAudioStats stats)
{
    bool restarted = !window->started || stats.elapsed_us == 0 ||
                     stats.elapsed_us < window->elapsed_us || stats.busy_us < window->busy_us;
    if (restarted) {
        *window = (LoadWindow){.started = stats.elapsed_us != 0,
                               .busy_us = stats.busy_us,
                               .elapsed_us = stats.elapsed_us};
        return;
    }
    uint32_t elapsed = stats.elapsed_us - window->elapsed_us;
    if (elapsed < LOAD_WINDOW_US) {
        return;
    }
    uint64_t busy = (uint64_t)(stats.busy_us - window->busy_us) * LOAD_PERCENT;
    window->percent = (uint32_t)(busy / elapsed);
    window->measured = true;
    window->busy_us = stats.busy_us;
    window->elapsed_us = stats.elapsed_us;
}

void diagnostics_cpu_text(uint32_t mhz, const LoadWindow *window, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, mhz);
    string_builder_append(&builder, " MHz");
    if (window->measured) {
        string_builder_append(&builder, " ");
        string_builder_append_unsigned(&builder, window->percent);
        string_builder_append(&builder, "%");
    }
}

void diagnostics_busy_text(const LoadWindow *window, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (!window->measured) {
        string_builder_append(&builder, "--");
        return;
    }
    string_builder_append_unsigned(&builder, window->percent);
    string_builder_append(&builder, "%");
}

void diagnostics_battery_text(HalBattery battery, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (!battery.known) {
        string_builder_append(&builder, i18n_text(STR_ABOUT_NO_READING));
    } else if (battery.usb_power) {
        i18n_format_counts(&builder, battery.charging ? STR_ABOUT_CHARGING : STR_ABOUT_FULL,
                           battery.millivolts, 0);
    } else {
        string_builder_append_unsigned(&builder, battery.millivolts);
        string_builder_append(&builder, " mV");
    }
}

void diagnostics_jack_pin_text(HalJackPin pin, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append(&builder, pin.high ? "high, boot fn" : "low, boot fn");
    string_builder_append_unsigned(&builder, pin.boot_function);
    string_builder_append(&builder, pin.boot_output ? " out" : " in");
    if (pin.pull) {
        string_builder_append(&builder, " pull");
    }
}

static const char *headset_button_name(HalHeadsetButton button)
{
    switch (button) {
    case HAL_HEADSET_BUTTON_PLAY:
        return " play";
    case HAL_HEADSET_BUTTON_VOLUME_UP:
        return " vol+";
    case HAL_HEADSET_BUTTON_VOLUME_DOWN:
        return " vol-";
    case HAL_HEADSET_BUTTON_NONE:
        break;
    }
    return " none";
}

void diagnostics_headset_text(HalHeadsetReading reading, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (!reading.known) {
        string_builder_append(&builder, i18n_text(STR_ABOUT_NOT_READ));
        return;
    }
    string_builder_append_unsigned(&builder, reading.level);
    if (reading.no_mic) {
        string_builder_append(&builder, " no mic");
        return;
    }
    if (!reading.dipped) {
        string_builder_append(&builder, headset_button_name(reading.button));
        return;
    }
    string_builder_append(&builder, " min ");
    string_builder_append_unsigned(&builder, reading.lowest);
    string_builder_append(&builder, " ");
    string_builder_append_unsigned(&builder, reading.dip_ms);
    string_builder_append(&builder, "ms ");
    string_builder_append_unsigned(&builder, reading.dips);
    string_builder_append(&builder, reading.dips == 1 ? " dip" : " dips");
}
