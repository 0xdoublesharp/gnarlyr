#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    SIM_DEFAULT_SCALE = 3,
    SIM_MAX_SCALE = 8,
    SIM_DEFAULT_BATTERY_PERCENT = 80,
    SIM_FULL_BATTERY_PERCENT = 100
};

typedef enum SimClockMode {
    SIM_CLOCK_UNSET,
    SIM_CLOCK_NOW,
    SIM_CLOCK_FIXED
} SimClockMode;

enum {
    SIM_OFFSET_WEST = 720,
    SIM_OFFSET_EAST = 840
};

typedef struct SimOptions {
    int scale;
    uint32_t frame_limit;
    bool headless;
    const char *screenshot_path;
    const char *key_script;
    const char *card_directory;
    const char *system_directory;
    uint8_t battery_percent;
    bool battery_charging;
    uint32_t usb_host_frame;
    const char *notice_title;
    const char *notice_text;
    SimClockMode clock_mode;
    uint32_t clock_utc_s;
    int16_t clock_offset_minutes;
    bool clock_offset_given;
} SimOptions;

bool sim_options_parse(int argument_count, char **arguments, SimOptions *options);
