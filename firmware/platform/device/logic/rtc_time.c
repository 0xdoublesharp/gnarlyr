#include "device/rtc_time.h"

#include "core/calendar.h"

enum {
    MINUTE_SECONDS = 60,
    HOUR_SECONDS = 3600
};

bool rtc_time_registers(uint32_t utc_s, uint8_t registers[RTC_TIME_REGISTERS])
{
    CalendarDate date = calendar_date(utc_s / CALENDAR_DAY_SECONDS);
    uint32_t of_day = utc_s % CALENDAR_DAY_SECONDS;
    RtcTime time = {.year = date.year,
                    .month = date.month,
                    .day = date.day,
                    .hour = (uint8_t)(of_day / HOUR_SECONDS),
                    .minute = (uint8_t)(of_day % HOUR_SECONDS / MINUTE_SECONDS),
                    .second = (uint8_t)(of_day % MINUTE_SECONDS),
                    .weekday = (uint8_t)((date.weekday + 1) % CALENDAR_WEEK_DAYS)};
    return rtc_registers_encode(&time, registers);
}

bool rtc_time_utc(const uint8_t registers[RTC_TIME_REGISTERS], uint32_t *utc_s)
{
    RtcTime time;
    if (!rtc_registers_decode(registers, &time) ||
        time.day > calendar_month_days((CalendarMonth){time.year, time.month})) {
        return false;
    }
    uint32_t days = calendar_days((CalendarDate){time.year, time.month, time.day, 0});
    *utc_s = days * CALENDAR_DAY_SECONDS + time.hour * HOUR_SECONDS + time.minute * MINUTE_SECONDS +
             time.second;
    return true;
}
