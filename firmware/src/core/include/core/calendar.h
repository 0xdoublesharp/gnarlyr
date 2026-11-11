#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    CALENDAR_FIRST_YEAR = 1970,
    CALENDAR_LAST_YEAR = 2105,
    CALENDAR_WEEK_DAYS = 7,
    CALENDAR_MONTHS = 12,
    CALENDAR_MAX_WEEKS = 6,
    CALENDAR_DAY_SECONDS = 86400
};

typedef struct CalendarDate {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
} CalendarDate;

typedef struct CalendarMonth {
    uint16_t year;
    uint8_t month;
} CalendarMonth;

typedef struct CalendarGrid {
    uint8_t first_column;
    uint8_t days;
    uint8_t weeks;
} CalendarGrid;

bool calendar_leap_year(uint16_t year);
uint8_t calendar_month_days(CalendarMonth month);
uint32_t calendar_days(CalendarDate date);
CalendarDate calendar_date(uint32_t days);
CalendarMonth calendar_step(CalendarMonth month, int months);
CalendarGrid calendar_grid(CalendarMonth month);
