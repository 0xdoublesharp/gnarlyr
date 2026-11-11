#include "core/calendar.h"

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
