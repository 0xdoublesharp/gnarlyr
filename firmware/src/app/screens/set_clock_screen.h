#pragma once

#include "core/calendar.h"
#include "hal/clock.h"
#include "settings/settings.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SET_CLOCK_YEAR,
    SET_CLOCK_MONTH,
    SET_CLOCK_DAY,
    SET_CLOCK_HOUR,
    SET_CLOCK_MINUTE,
    SET_CLOCK_OFFSET,
    SET_CLOCK_FIELDS,
    SET_CLOCK_TEXT_CAPACITY = 64
};

typedef struct SetClockConfig {
    const Theme *theme;
    const Settings *settings;
    Toast *toast;
} SetClockConfig;

typedef struct SetClockScreen {
    Screen screen;
    SetClockConfig config;
    CalendarDate date;
    int16_t offset_minutes;
    uint8_t hour;
    uint8_t minute;
    uint8_t field;
    char text[SET_CLOCK_TEXT_CAPACITY];
} SetClockScreen;

Screen *set_clock_screen_init(SetClockScreen *editor, SetClockConfig config);
const char *set_clock_screen_summary(const Settings *settings);
