#include "check.h"
#include "device/rtc_registers.h"
#include "device/rtc_time.h"

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

static void a_time_encodes_as_bcd_with_the_trust_flag_clear(void)
{
    RtcTime time = {
        .year = 2026, .month = 11, .day = 11, .hour = 9, .minute = 41, .second = 7, .weekday = 3};
    uint8_t registers[RTC_TIME_REGISTERS] = {0xAA, 0xBB};
    CHECK(rtc_registers_encode(&time, registers));
    CHECK_EQUAL(0xAA, registers[0]);
    CHECK_EQUAL(0xBB, registers[1]);
    CHECK_EQUAL(0x07, registers[RTC_REGISTER_SECONDS]);
    CHECK_EQUAL(0x41, registers[RTC_REGISTER_MINUTES]);
    CHECK_EQUAL(0x09, registers[RTC_REGISTER_HOURS]);
    CHECK_EQUAL(0x11, registers[RTC_REGISTER_DAYS]);
    CHECK_EQUAL(0x03, registers[RTC_REGISTER_WEEKDAYS]);
    CHECK_EQUAL(0x11, registers[RTC_REGISTER_MONTHS]);
    CHECK_EQUAL(0x26, registers[RTC_REGISTER_YEARS]);
    RtcTime back;
    CHECK(rtc_registers_decode(registers, &back));
    CHECK(!back.voltage_low);
    CHECK_EQUAL(2026, back.year);
    CHECK_EQUAL(7, back.second);
}

static void a_time_the_chip_cannot_hold_is_not_encoded(void)
{
    RtcTime times[] = {{.year = 1999, .month = 1, .day = 1},
                       {.year = 2100, .month = 1, .day = 1},
                       {.year = 2026, .month = 13, .day = 1},
                       {.year = 2026, .month = 1, .day = 0},
                       {.year = 2026, .month = 1, .day = 1, .hour = 24},
                       {.year = 2026, .month = 1, .day = 1, .second = 60}};
    for (unsigned index = 0; index < sizeof times / sizeof times[0]; index++) {
        uint8_t registers[RTC_TIME_REGISTERS];
        CHECK(!rtc_registers_encode(&times[index], registers));
    }
}

static void utc_seconds_go_to_the_chip_and_back_unchanged(void)
{
    static const uint32_t TIMES[] = {946684800u, 951782400u, 1794390067u, 4102444799u};
    for (unsigned index = 0; index < sizeof TIMES / sizeof TIMES[0]; index++) {
        uint8_t registers[RTC_TIME_REGISTERS] = {0};
        uint32_t back = 0;
        CHECK(rtc_time_registers(TIMES[index], registers));
        CHECK(rtc_time_utc(registers, &back));
        CHECK_EQUAL(TIMES[index], back);
    }
    uint8_t registers[RTC_TIME_REGISTERS] = {0};
    CHECK(rtc_time_registers(1794390060u, registers));
    CHECK_EQUAL(3, registers[RTC_REGISTER_WEEKDAYS]);
    CHECK(!rtc_time_registers(946684799u, registers));
}

static void a_day_past_the_end_of_its_month_has_no_utc_time(void)
{
    uint8_t registers[RTC_TIME_REGISTERS] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x31, 0x00, 0x11, 0x26};
    uint32_t utc = 0;
    CHECK(!rtc_time_utc(registers, &utc));
    registers[RTC_REGISTER_DAYS] = 0x30;
    CHECK(rtc_time_utc(registers, &utc));
}

static void startup_trust_needs_a_valid_recent_time_and_a_clear_voltage_flag(void)
{
    uint8_t registers[RTC_TIME_REGISTERS];
    for (int byte = 0; byte < RTC_TIME_REGISTERS; byte++) {
        registers[byte] = STOCK_DEFAULT[byte];
    }
    uint32_t utc = 0;
    CHECK(rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_SECONDS] |= 0x80;
    CHECK(!rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_SECONDS] &= 0x7F;
    registers[RTC_REGISTER_MINUTES] = 0x1A;
    CHECK(!rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_MINUTES] = 0;
    registers[RTC_REGISTER_YEARS] = 0x23;
    CHECK(!rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_YEARS] = 0x24;
    CHECK(rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_YEARS] = 0x99;
    CHECK(rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_YEARS] = 0xA0;
    CHECK(!rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_YEARS] = 0x25;
    registers[RTC_REGISTER_MONTHS] = 0x02;
    registers[RTC_REGISTER_DAYS] = 0x29;
    CHECK(!rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_YEARS] = 0x24;
    CHECK(rtc_time_trusted(registers, &utc));
    registers[RTC_REGISTER_WEEKDAYS] = 7;
    CHECK(!rtc_time_trusted(registers, &utc));
}

static void the_player_reading_decodes_whatever_its_unused_bits_hold(void)
{
    static const uint8_t FIRST[16] = {0x00, 0x00, 0x06, 0x41, 0x22, 0x20, 0x00, 0x05,
                                      0x26, 0x80, 0x90, 0x94, 0x90, 0x84, 0x07, 0x00};
    static const uint8_t SECOND[16] = {0x00, 0x00, 0x11, 0x41, 0x22, 0x20, 0x10, 0x05,
                                       0x26, 0x80, 0x90, 0x94, 0x90, 0x90, 0x13, 0x00};
    uint32_t first = 0;
    uint32_t second = 0;
    CHECK(rtc_time_utc(FIRST, &first));
    CHECK(rtc_time_utc(SECOND, &second));
    CHECK_EQUAL(5, second - first);
    RtcTime time;
    CHECK(rtc_registers_decode(SECOND, &time));
    CHECK_EQUAL(0, time.weekday);
    CHECK(!time.voltage_low);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_registers_read_as_bcd_from_the_seconds_register_on),
        TEST_CASE(the_flag_bits_are_kept_apart_from_the_time),
        TEST_CASE(a_digit_above_nine_or_a_field_out_of_range_is_not_a_time),
        TEST_CASE(a_bus_that_reads_all_ones_is_not_a_time),
        TEST_CASE(a_time_encodes_as_bcd_with_the_trust_flag_clear),
        TEST_CASE(a_time_the_chip_cannot_hold_is_not_encoded),
        TEST_CASE(utc_seconds_go_to_the_chip_and_back_unchanged),
        TEST_CASE(a_day_past_the_end_of_its_month_has_no_utc_time),
        TEST_CASE(startup_trust_needs_a_valid_recent_time_and_a_clear_voltage_flag),
        TEST_CASE(the_player_reading_decodes_whatever_its_unused_bits_hold),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
