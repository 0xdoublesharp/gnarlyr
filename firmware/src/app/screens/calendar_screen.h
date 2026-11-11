#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/calendar.h"
#include "gfx/font.h"
#include "gfx/rect.h"
#include "ui/screen.h"
#include "ui/theme.h"

typedef struct CalendarScreenConfig {
    const Theme *theme;
} CalendarScreenConfig;

typedef struct CalendarScreen {
    Screen screen;
    CalendarScreenConfig config;
    CalendarMonth shown;
} CalendarScreen;

typedef struct CalendarLayout {
    Rect names;
    Rect grid;
    Rect hint;
    int cell_width;
    int cell_height;
    const Font *font;
} CalendarLayout;

Screen *calendar_screen_init(CalendarScreen *calendar, CalendarScreenConfig config);
CalendarLayout calendar_screen_layout(const Theme *theme, uint8_t weeks);
Rect calendar_screen_cell(const CalendarLayout *layout, int index);
