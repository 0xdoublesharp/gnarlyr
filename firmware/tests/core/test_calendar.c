#include "check.h"
#include "core/calendar.h"

enum {
    NOVEMBER_11_2026 = 20768,
    THURSDAY = 3,
    WEDNESDAY = 2,
    SUNDAY = 6,
    MONDAY = 0,
    SATURDAY = 5
};

static void leap_years_follow_the_gregorian_rules(void)
{
    CHECK(calendar_leap_year(2000));
    CHECK(calendar_leap_year(2024));
    CHECK(calendar_leap_year(2028));
    CHECK(!calendar_leap_year(1900));
    CHECK(!calendar_leap_year(2026));
    CHECK(!calendar_leap_year(2100));
    CHECK_EQUAL(29, calendar_month_days((CalendarMonth){2024, 2}));
    CHECK_EQUAL(28, calendar_month_days((CalendarMonth){2026, 2}));
    CHECK_EQUAL(28, calendar_month_days((CalendarMonth){2100, 2}));
    CHECK_EQUAL(30, calendar_month_days((CalendarMonth){2026, 11}));
    CHECK_EQUAL(31, calendar_month_days((CalendarMonth){2026, 12}));
}

static void day_zero_is_thursday_the_first_of_january_1970(void)
{
    CalendarDate date = calendar_date(0);
    CHECK_EQUAL(1970, date.year);
    CHECK_EQUAL(1, date.month);
    CHECK_EQUAL(1, date.day);
    CHECK_EQUAL(THURSDAY, date.weekday);
}

static void known_dates_land_on_their_day_and_weekday(void)
{
    CalendarDate today = calendar_date(NOVEMBER_11_2026);
    CHECK_EQUAL(2026, today.year);
    CHECK_EQUAL(11, today.month);
    CHECK_EQUAL(11, today.day);
    CHECK_EQUAL(WEDNESDAY, today.weekday);
    CHECK_EQUAL(NOVEMBER_11_2026, calendar_days((CalendarDate){2026, 11, 11, 0}));
    CalendarDate leap = calendar_date(calendar_days((CalendarDate){2000, 2, 29, 0}));
    CHECK_EQUAL(2, leap.month);
    CHECK_EQUAL(29, leap.day);
    CHECK_EQUAL(1, calendar_date(calendar_days((CalendarDate){2100, 2, 28, 0}) + 1).day);
}

static void every_day_from_1970_to_2105_converts_both_ways(void)
{
    uint32_t last = calendar_days((CalendarDate){CALENDAR_LAST_YEAR, 12, 31, 0});
    CalendarDate previous = calendar_date(0);
    for (uint32_t days = 1; days <= last; days++) {
        CalendarDate date = calendar_date(days);
        if (calendar_days(date) != days || date.weekday != (previous.weekday + 1) % 7) {
            CHECK_EQUAL(days, calendar_days(date));
            return;
        }
        previous = date;
    }
    CHECK_EQUAL(CALENDAR_LAST_YEAR, previous.year);
    CHECK_EQUAL(31, previous.day);
}

static void a_month_grid_starts_on_monday(void)
{
    CalendarGrid november = calendar_grid((CalendarMonth){2026, 11});
    CHECK_EQUAL(SUNDAY, november.first_column);
    CHECK_EQUAL(30, november.days);
    CHECK_EQUAL(6, november.weeks);
    CalendarGrid february = calendar_grid((CalendarMonth){2021, 2});
    CHECK_EQUAL(MONDAY, february.first_column);
    CHECK_EQUAL(4, february.weeks);
    CalendarGrid leap = calendar_grid((CalendarMonth){2016, 2});
    CHECK_EQUAL(29, leap.days);
    CHECK_EQUAL(MONDAY, leap.first_column);
    CHECK_EQUAL(5, leap.weeks);
    CalendarGrid august = calendar_grid((CalendarMonth){2026, 8});
    CHECK_EQUAL(SATURDAY, august.first_column);
    CHECK_EQUAL(6, august.weeks);
}

static void stepping_months_crosses_years_and_stops_at_the_ends(void)
{
    CalendarMonth january = calendar_step((CalendarMonth){2026, 12}, 1);
    CHECK_EQUAL(2027, january.year);
    CHECK_EQUAL(1, january.month);
    CalendarMonth december = calendar_step((CalendarMonth){2026, 1}, -1);
    CHECK_EQUAL(2025, december.year);
    CHECK_EQUAL(12, december.month);
    CalendarMonth first = calendar_step((CalendarMonth){1970, 1}, -1);
    CHECK_EQUAL(1970, first.year);
    CHECK_EQUAL(1, first.month);
    CalendarMonth last = calendar_step((CalendarMonth){CALENDAR_LAST_YEAR, 12}, 1);
    CHECK_EQUAL(CALENDAR_LAST_YEAR, last.year);
    CHECK_EQUAL(12, last.month);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(leap_years_follow_the_gregorian_rules),
        TEST_CASE(day_zero_is_thursday_the_first_of_january_1970),
        TEST_CASE(known_dates_land_on_their_day_and_weekday),
        TEST_CASE(every_day_from_1970_to_2105_converts_both_ways),
        TEST_CASE(a_month_grid_starts_on_monday),
        TEST_CASE(stepping_months_crosses_years_and_stops_at_the_ends),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
