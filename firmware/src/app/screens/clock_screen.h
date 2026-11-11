#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/strings.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "gfx/rect.h"
#include "settings/settings.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    CLOCK_SCREEN_MAX_SCALE = 3,
    CLOCK_SCREEN_TEXT = 64
};

typedef struct ClockScreenConfig {
    const Theme *theme;
    const Settings *settings;
    Screen *calendar;
} ClockScreenConfig;

typedef struct ClockScreen {
    Screen screen;
    ClockScreenConfig config;
} ClockScreen;

typedef struct ClockLayout {
    Rect weekday;
    Rect time;
    Rect date;
    Rect hint;
    int scale;
} ClockLayout;

Screen *clock_screen_init(ClockScreen *clock, ClockScreenConfig config);
ClockLayout clock_screen_layout(const Theme *theme, const char *digits, const char *meridiem);
void clock_screen_date_text(uint32_t local_s, char *text, int capacity);
const Font *clock_screen_date_font(const Theme *theme, const char *date, int width);
StringId clock_screen_weekday(uint8_t weekday);
void clock_screen_draw_unknown(Canvas *canvas, const Theme *theme, Rect content);
StringId clock_screen_month(uint8_t month);
