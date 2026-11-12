#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/strings.h"
#include "hal/power.h"
#include "settings/settings.h"
#include "ui/icons.h"

typedef enum PlugMode {
    PLUG_MODE_ASK,
    PLUG_MODE_COMPUTER,
    PLUG_MODE_CHARGE,
    PLUG_MODE_USB_POWER,
    PLUG_MODE_DAC,
    PLUG_MODE_COUNT
} PlugMode;

typedef enum PlugPlace {
    PLUG_PLACE_PROMPT,
    PLUG_PLACE_SWITCH,
    PLUG_PLACE_SETTINGS
} PlugPlace;

enum {
    PLUG_CARE_FULL = 0,
    PLUG_CARE_USB = 2,
    PLUG_SAFE_MILLIVOLTS = 3800,
    PLUG_NOT_MARKED = 255
};

typedef struct PlugState {
    PlugPlace place;
    PlugMode saved;
    uint8_t care;
    HalBattery battery;
} PlugState;

typedef struct PlugChoices {
    uint8_t count;
    uint8_t cursor;
    uint8_t marked;
    uint8_t modes[PLUG_MODE_COUNT];
} PlugChoices;

PlugChoices plug_modes_choices(PlugState state);
bool plug_modes_battery_ready(HalBattery battery);
StringId plug_mode_name(PlugMode mode);
StringId plug_mode_short_name(PlugMode mode);
StringId plug_mode_line(PlugMode mode, HalBattery battery);
IconId plug_mode_icon(PlugMode mode);
PlugMode plug_mode_saved(const Settings *settings);
void plug_mode_save(Settings *settings, PlugMode mode);
