#include "core/clock_face.h"

#include "core/i18n.h"

enum {
    MINUTE_SECONDS = 60,
    HOUR_SECONDS = 3600,
    DAY_SECONDS = 86400,
    HALF_DAY_HOURS = 12,
    TEN = 10,
    DIGITS_CAPACITY = 8
};

uint32_t clock_local_seconds(uint32_t utc_s, int16_t offset_minutes)
{
    int32_t offset = (int32_t)offset_minutes * MINUTE_SECONDS;
    return utc_s + (uint32_t)offset;
}

ClockFace clock_face(uint32_t local_s, bool twenty_four_hour)
{
    uint32_t of_day = local_s % DAY_SECONDS;
    uint8_t hour = (uint8_t)(of_day / HOUR_SECONDS);
    ClockFace face = {.hour = hour,
                      .minute = (uint8_t)(of_day % HOUR_SECONDS / MINUTE_SECONDS),
                      .second = (uint8_t)(of_day % MINUTE_SECONDS),
                      .meridiem = CLOCK_TWENTY_FOUR_HOUR};
    if (!twenty_four_hour) {
        face.meridiem = hour < HALF_DAY_HOURS ? CLOCK_AM : CLOCK_PM;
        face.hour = hour % HALF_DAY_HOURS == 0 ? HALF_DAY_HOURS : hour % HALF_DAY_HOURS;
    }
    return face;
}

static void append_two(StringBuilder *builder, uint8_t value)
{
    string_builder_append_char(builder, (char)('0' + value / TEN));
    string_builder_append_char(builder, (char)('0' + value % TEN));
}

void clock_face_digits(StringBuilder *builder, ClockFace face)
{
    if (face.meridiem == CLOCK_TWENTY_FOUR_HOUR) {
        append_two(builder, face.hour);
    } else {
        string_builder_append_unsigned(builder, face.hour);
    }
    string_builder_append_char(builder, ':');
    append_two(builder, face.minute);
}

StringId clock_face_meridiem(ClockFace face)
{
    return face.meridiem == CLOCK_AM ? STR_CLOCK_AM : STR_CLOCK_PM;
}

void clock_face_text(StringBuilder *builder, ClockFace face)
{
    if (face.meridiem == CLOCK_TWENTY_FOUR_HOUR) {
        clock_face_digits(builder, face);
        return;
    }
    char digits[DIGITS_CAPACITY];
    StringBuilder inner = string_builder_make(digits, sizeof digits);
    clock_face_digits(&inner, face);
    const char *arguments[] = {digits, i18n_text(clock_face_meridiem(face))};
    i18n_format(builder, STR_CLOCK_12_HOUR, arguments, 2);
}
