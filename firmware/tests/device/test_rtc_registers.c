#include "check.h"
#include "device/rtc_registers.h"

static const uint8_t STOCK_DEFAULT[RTC_TIME_REGISTERS] = {0x00, 0x00, 0x00, 0x00, 0x12,
                                                          0x03, 0x06, 0x01, 0x26};

static void the_registers_read_as_bcd_from_the_seconds_register_on(void)
{
    RtcTime time;
    CHECK(rtc_registers_decode(STOCK_DEFAULT, &time));
    CHECK_EQUAL(2026, time.year);
    CHECK_EQUAL(1, time.month);
    CHECK_EQUAL(3, time.day);
    CHECK_EQUAL(12, time.hour);
    CHECK_EQUAL(0, time.minute);
    CHECK_EQUAL(0, time.second);
    CHECK_EQUAL(6, time.weekday);
    CHECK(!time.voltage_low);
    CHECK(!time.century);
}

static void the_flag_bits_are_kept_apart_from_the_time(void)
{
    uint8_t registers[RTC_TIME_REGISTERS] = {0x08, 0x00, 0xD9, 0xD9, 0xE3, 0xF1, 0xFE, 0x92, 0x99};
    RtcTime time;
    CHECK(rtc_registers_decode(registers, &time));
    CHECK(time.voltage_low);
    CHECK(time.century);
    CHECK_EQUAL(59, time.second);
    CHECK_EQUAL(59, time.minute);
    CHECK_EQUAL(23, time.hour);
    CHECK_EQUAL(31, time.day);
    CHECK_EQUAL(6, time.weekday);
    CHECK_EQUAL(12, time.month);
    CHECK_EQUAL(2099, time.year);
}

static void a_digit_above_nine_or_a_field_out_of_range_is_not_a_time(void)
{
    static const struct {
        uint8_t index;
        uint8_t value;
    } BAD[] = {
        {RTC_REGISTER_SECONDS, 0x60}, {RTC_REGISTER_MINUTES, 0x0A}, {RTC_REGISTER_HOURS, 0x24},
        {RTC_REGISTER_DAYS, 0x00},    {RTC_REGISTER_DAYS, 0x32},    {RTC_REGISTER_MONTHS, 0x00},
        {RTC_REGISTER_MONTHS, 0x13},  {RTC_REGISTER_YEARS, 0xA0},
    };
    for (unsigned index = 0; index < sizeof BAD / sizeof BAD[0]; index++) {
        uint8_t registers[RTC_TIME_REGISTERS];
        for (int byte = 0; byte < RTC_TIME_REGISTERS; byte++) {
            registers[byte] = STOCK_DEFAULT[byte];
        }
        registers[BAD[index].index] = BAD[index].value;
        RtcTime time;
        CHECK(!rtc_registers_decode(registers, &time));
    }
}

static void a_bus_that_reads_all_ones_is_not_a_time(void)
{
    uint8_t registers[RTC_TIME_REGISTERS] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    RtcTime time;
    CHECK(!rtc_registers_decode(registers, &time));
    CHECK(time.voltage_low);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_registers_read_as_bcd_from_the_seconds_register_on),
        TEST_CASE(the_flag_bits_are_kept_apart_from_the_time),
        TEST_CASE(a_digit_above_nine_or_a_field_out_of_range_is_not_a_time),
        TEST_CASE(a_bus_that_reads_all_ones_is_not_a_time),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
