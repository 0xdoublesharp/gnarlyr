#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    RTC_REGISTER_SECONDS = 2,
    RTC_REGISTER_MINUTES = 3,
    RTC_REGISTER_HOURS = 4,
    RTC_REGISTER_DAYS = 5,
    RTC_REGISTER_WEEKDAYS = 6,
    RTC_REGISTER_MONTHS = 7,
    RTC_REGISTER_YEARS = 8,
    RTC_TIME_REGISTERS = 9,
    RTC_FIRST_YEAR = 2000,
    RTC_LAST_YEAR = 2099
};

typedef struct RtcTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
    bool century;
    bool voltage_low;
} RtcTime;

bool rtc_registers_decode(const uint8_t registers[RTC_TIME_REGISTERS], RtcTime *time);
bool rtc_registers_encode(const RtcTime *time, uint8_t registers[RTC_TIME_REGISTERS]);
