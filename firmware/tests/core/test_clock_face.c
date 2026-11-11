#include <string.h>

#include "check.h"
#include "core/clock_face.h"
#include "core/i18n.h"

enum {
    TEXT_CAPACITY = 32,
    HOUR = 3600,
    MINUTE = 60
};

static const char *text_of(uint32_t local_s, bool twenty_four_hour)
{
    static char text[TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    clock_face_text(&builder, clock_face(local_s, twenty_four_hour));
    return text;
}

static void twenty_four_hour_time_has_two_digit_hours(void)
{
    CHECK(strcmp(text_of(9 * HOUR + 5 * MINUTE, true), "09:05") == 0);
    CHECK(strcmp(text_of(0, true), "00:00") == 0);
    CHECK(strcmp(text_of(23 * HOUR + 59 * MINUTE + 59, true), "23:59") == 0);
    CHECK(strcmp(text_of(86400 * 3 + 13 * HOUR, true), "13:00") == 0);
}

static void twelve_hour_time_names_midnight_and_noon_twelve(void)
{
    CHECK(strcmp(text_of(0, false), "12:00 AM") == 0);
    CHECK(strcmp(text_of(12 * HOUR, false), "12:00 PM") == 0);
    CHECK(strcmp(text_of(9 * HOUR + 41 * MINUTE, false), "9:41 AM") == 0);
    CHECK(strcmp(text_of(23 * HOUR + 59 * MINUTE, false), "11:59 PM") == 0);
}

static void the_face_keeps_the_seconds_and_the_marker(void)
{
    ClockFace face = clock_face(13 * HOUR + 4 * MINUTE + 7, false);
    CHECK_EQUAL(1, face.hour);
    CHECK_EQUAL(4, face.minute);
    CHECK_EQUAL(7, face.second);
    CHECK_EQUAL(CLOCK_PM, face.meridiem);
    CHECK_EQUAL(STR_CLOCK_PM, clock_face_meridiem(face));
    CHECK_EQUAL(CLOCK_TWENTY_FOUR_HOUR, clock_face(0, true).meridiem);
}

static void local_seconds_add_the_offset_in_both_directions(void)
{
    CHECK_EQUAL(1794482460u + 3600u, clock_local_seconds(1794482460u, 60));
    CHECK_EQUAL(1794482460u - 28800u, clock_local_seconds(1794482460u, -480));
    CHECK_EQUAL(1794482460u, clock_local_seconds(1794482460u, 0));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(twenty_four_hour_time_has_two_digit_hours),
        TEST_CASE(twelve_hour_time_names_midnight_and_noon_twelve),
        TEST_CASE(the_face_keeps_the_seconds_and_the_marker),
        TEST_CASE(local_seconds_add_the_offset_in_both_directions),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
