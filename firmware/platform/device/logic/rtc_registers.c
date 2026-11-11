#include "device/rtc_registers.h"

enum {
    BCD_DIGIT = 0x0F,
    BCD_TENS_SHIFT = 4,
    DECIMAL = 10,
    VOLTAGE_LOW = 0x80,
    CENTURY = 0x80,
    SECONDS_MASK = 0x7F,
    MINUTES_MASK = 0x7F,
    HOURS_MASK = 0x3F,
    DAYS_MASK = 0x3F,
    WEEKDAYS_MASK = 0x07,
    MONTHS_MASK = 0x1F,
    MAX_SECOND = 59,
    MAX_MINUTE = 59,
    MAX_HOUR = 23,
    MAX_DAY = 31,
    MAX_MONTH = 12
};

static bool bcd(uint8_t value, uint8_t highest, uint8_t *decoded)
{
    uint8_t ones = value & BCD_DIGIT;
    uint8_t tens = (uint8_t)(value >> BCD_TENS_SHIFT);
    *decoded = (uint8_t)(tens * DECIMAL + ones);
    return ones < DECIMAL && tens < DECIMAL && *decoded <= highest;
}

bool rtc_registers_decode(const uint8_t registers[RTC_TIME_REGISTERS], RtcTime *time)
{
    uint8_t year = 0;
    *time = (RtcTime){
        .weekday = registers[RTC_REGISTER_WEEKDAYS] & WEEKDAYS_MASK,
        .century = (registers[RTC_REGISTER_MONTHS] & CENTURY) != 0,
        .voltage_low = (registers[RTC_REGISTER_SECONDS] & VOLTAGE_LOW) != 0,
    };
    bool valid = bcd(registers[RTC_REGISTER_SECONDS] & SECONDS_MASK, MAX_SECOND, &time->second) &&
                 bcd(registers[RTC_REGISTER_MINUTES] & MINUTES_MASK, MAX_MINUTE, &time->minute) &&
                 bcd(registers[RTC_REGISTER_HOURS] & HOURS_MASK, MAX_HOUR, &time->hour) &&
                 bcd(registers[RTC_REGISTER_DAYS] & DAYS_MASK, MAX_DAY, &time->day) &&
                 bcd(registers[RTC_REGISTER_MONTHS] & MONTHS_MASK, MAX_MONTH, &time->month) &&
                 bcd(registers[RTC_REGISTER_YEARS], DECIMAL * DECIMAL - 1, &year);
    time->year = (uint16_t)(RTC_FIRST_YEAR + year);
    return valid && time->day != 0 && time->month != 0;
}

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(value / DECIMAL << BCD_TENS_SHIFT | value % DECIMAL);
}

bool rtc_registers_encode(const RtcTime *time, uint8_t registers[RTC_TIME_REGISTERS])
{
    if (time->year < RTC_FIRST_YEAR || time->year > RTC_LAST_YEAR || time->month == 0 ||
        time->month > MAX_MONTH || time->day == 0 || time->day > MAX_DAY || time->hour > MAX_HOUR ||
        time->minute > MAX_MINUTE || time->second > MAX_SECOND) {
        return false;
    }
    registers[RTC_REGISTER_SECONDS] = to_bcd(time->second);
    registers[RTC_REGISTER_MINUTES] = to_bcd(time->minute);
    registers[RTC_REGISTER_HOURS] = to_bcd(time->hour);
    registers[RTC_REGISTER_DAYS] = to_bcd(time->day);
    registers[RTC_REGISTER_WEEKDAYS] = time->weekday & WEEKDAYS_MASK;
    registers[RTC_REGISTER_MONTHS] = to_bcd(time->month);
    registers[RTC_REGISTER_YEARS] = to_bcd((uint8_t)(time->year - RTC_FIRST_YEAR));
    return true;
}
