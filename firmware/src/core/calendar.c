#include "core/calendar.h"

enum {
    MARCH = 3,
    FEBRUARY = 2,
    LONG_MONTH = 31,
    SHORT_MONTH = 30,
    FEBRUARY_DAYS = 28,
    ERA_YEARS = 400,
    ERA_DAYS = 146097,
    CENTURY_YEARS = 100,
    LEAP_EVERY = 4,
    YEAR_DAYS = 365,
    MONTH_SPAN_NUMERATOR = 153,
    MONTH_SPAN_TERMS = 5,
    MONTH_SPAN_OFFSET = 2,
    MONTHS_AFTER_FEBRUARY = 10,
    DAYS_FROM_ERA_TO_EPOCH = 719468,
    EPOCH_WEEKDAY = 3,
    FOUR_YEAR_DAYS_BEFORE_LEAP = 1460,
    CENTURY_DAYS_BEFORE_LEAP = 36524
};

bool calendar_leap_year(uint16_t year)
{
    return year % LEAP_EVERY == 0 && (year % CENTURY_YEARS != 0 || year % ERA_YEARS == 0);
}

uint8_t calendar_month_days(CalendarMonth month)
{
    static const uint8_t DAYS[CALENDAR_MONTHS] = {
        LONG_MONTH, FEBRUARY_DAYS, LONG_MONTH,  SHORT_MONTH, LONG_MONTH,  SHORT_MONTH,
        LONG_MONTH, LONG_MONTH,    SHORT_MONTH, LONG_MONTH,  SHORT_MONTH, LONG_MONTH};
    uint8_t days = DAYS[month.month - 1];
    return month.month == FEBRUARY && calendar_leap_year(month.year) ? (uint8_t)(days + 1) : days;
}

uint32_t calendar_days(CalendarDate date)
{
    uint32_t year = date.month < MARCH ? date.year - 1u : date.year;
    uint32_t era = year / ERA_YEARS;
    uint32_t of_era = year - era * ERA_YEARS;
    uint32_t month = date.month < MARCH ? date.month + 9u : date.month - 3u;
    uint32_t of_year =
        (MONTH_SPAN_NUMERATOR * month + MONTH_SPAN_OFFSET) / MONTH_SPAN_TERMS + date.day - 1u;
    uint32_t of_cycle = of_era * YEAR_DAYS + of_era / LEAP_EVERY - of_era / CENTURY_YEARS + of_year;
    return era * ERA_DAYS + of_cycle - DAYS_FROM_ERA_TO_EPOCH;
}

CalendarDate calendar_date(uint32_t days)
{
    uint32_t shifted = days + DAYS_FROM_ERA_TO_EPOCH;
    uint32_t era = shifted / ERA_DAYS;
    uint32_t of_era = shifted - era * ERA_DAYS;
    uint32_t years = (of_era - of_era / FOUR_YEAR_DAYS_BEFORE_LEAP +
                      of_era / CENTURY_DAYS_BEFORE_LEAP - of_era / (ERA_DAYS - 1u)) /
                     YEAR_DAYS;
    uint32_t of_year = of_era - (YEAR_DAYS * years + years / LEAP_EVERY - years / CENTURY_YEARS);
    uint32_t month = (MONTH_SPAN_TERMS * of_year + MONTH_SPAN_OFFSET) / MONTH_SPAN_NUMERATOR;
    uint32_t day =
        of_year - (MONTH_SPAN_NUMERATOR * month + MONTH_SPAN_OFFSET) / MONTH_SPAN_TERMS + 1u;
    uint32_t civil_month = month < MONTHS_AFTER_FEBRUARY ? month + 3u : month - 9u;
    uint32_t year = years + era * ERA_YEARS + (civil_month < MARCH ? 1u : 0u);
    return (CalendarDate){.year = (uint16_t)year,
                          .month = (uint8_t)civil_month,
                          .day = (uint8_t)day,
                          .weekday = (uint8_t)((days + EPOCH_WEEKDAY) % CALENDAR_WEEK_DAYS)};
}

CalendarMonth calendar_step(CalendarMonth month, int months)
{
    int32_t index = (int32_t)month.year * CALENDAR_MONTHS + (month.month - 1) + months;
    int32_t first = CALENDAR_FIRST_YEAR * CALENDAR_MONTHS;
    int32_t last = CALENDAR_LAST_YEAR * CALENDAR_MONTHS + CALENDAR_MONTHS - 1;
    index = index < first ? first : index > last ? last : index;
    return (CalendarMonth){.year = (uint16_t)(index / CALENDAR_MONTHS),
                           .month = (uint8_t)(index % CALENDAR_MONTHS + 1)};
}

CalendarGrid calendar_grid(CalendarMonth month)
{
    CalendarDate first =
        calendar_date(calendar_days((CalendarDate){month.year, month.month, 1, 0}));
    uint8_t days = calendar_month_days(month);
    uint8_t cells = (uint8_t)(first.weekday + days);
    return (CalendarGrid){.first_column = first.weekday,
                          .days = days,
                          .weeks =
                              (uint8_t)((cells + CALENDAR_WEEK_DAYS - 1) / CALENDAR_WEEK_DAYS)};
}
