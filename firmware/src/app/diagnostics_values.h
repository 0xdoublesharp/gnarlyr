#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "hal/audio.h"
#include "hal/input.h"
#include "hal/power.h"
#include "hal/system.h"

enum {
    DIAGNOSTICS_VALUE_CAPACITY = 32
};

typedef struct LoadWindow {
    bool started;
    bool measured;
    uint32_t percent;
    uint32_t busy_us;
    uint32_t elapsed_us;
} LoadWindow;

uint32_t diagnostics_next_clock_mhz(uint32_t current_mhz);
void diagnostics_clock_text(uint32_t mhz, bool failed, char *text, size_t capacity);
void diagnostics_load_update(LoadWindow *window, HalAudioStats stats);
void diagnostics_cpu_text(uint32_t mhz, const LoadWindow *window, char *text, size_t capacity);
void diagnostics_busy_text(const LoadWindow *window, char *text, size_t capacity);
void diagnostics_battery_text(HalBattery battery, char *text, size_t capacity);
void diagnostics_jack_pin_text(HalJackPin pin, char *text, size_t capacity);
void diagnostics_headset_text(HalHeadsetReading reading, char *text, size_t capacity);
