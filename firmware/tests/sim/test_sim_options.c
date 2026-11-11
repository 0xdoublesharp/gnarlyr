#include <stddef.h>

#include "check.h"
#include "sim_options.h"

enum {
    MAX_ARGUMENTS = 8
};

static bool parse(char **given, int count, SimOptions *options)
{
    char *arguments[MAX_ARGUMENTS] = {"echo_sim"};
    for (int index = 0; index < count; index++) {
        arguments[index + 1] = given[index];
    }
    return sim_options_parse(count + 1, arguments, options);
}

static void no_arguments_give_a_windowed_run_at_default_scale(void)
{
    SimOptions options;
    CHECK(parse(NULL, 0, &options));
    CHECK_EQUAL(SIM_DEFAULT_SCALE, options.scale);
    CHECK_EQUAL(0, options.frame_limit);
    CHECK(!options.headless);
    CHECK(options.screenshot_path == NULL);
    CHECK(options.key_script == NULL);
    CHECK(options.card_directory == NULL);
    CHECK(options.system_directory == NULL);
    CHECK_EQUAL(SIM_DEFAULT_BATTERY_PERCENT, options.battery_percent);
    CHECK(!options.battery_charging);
}

static void the_battery_level_and_charging_are_read(void)
{
    char *given[] = {"--battery", "12", "--charging"};
    SimOptions options;
    CHECK(parse(given, 3, &options));
    CHECK_EQUAL(12, options.battery_percent);
    CHECK(options.battery_charging);
}

static void the_clock_is_unset_unless_asked_for(void)
{
    SimOptions options;
    CHECK(parse(NULL, 0, &options));
    CHECK_EQUAL(SIM_CLOCK_UNSET, options.clock_mode);
}

static void the_clock_reads_now_or_utc_seconds_with_an_offset(void)
{
    char *now[] = {"--clock", "now"};
    char *fixed[] = {"--clock", "1794482460", "--utc-offset", "-480"};
    SimOptions options;
    CHECK(parse(now, 2, &options));
    CHECK_EQUAL(SIM_CLOCK_NOW, options.clock_mode);
    CHECK(parse(fixed, 4, &options));
    CHECK_EQUAL(SIM_CLOCK_FIXED, options.clock_mode);
    CHECK_EQUAL(1794482460u, options.clock_utc_s);
    CHECK_EQUAL(-480, options.clock_offset_minutes);
}

static void an_offset_needs_a_fixed_clock_and_stays_within_a_day(void)
{
    char *alone[] = {"--utc-offset", "60"};
    char *with_now[] = {"--clock", "now", "--utc-offset", "60"};
    char *too_far[] = {"--clock", "1794482460", "--utc-offset", "841"};
    char *not_a_time[] = {"--clock", "soon"};
    SimOptions options;
    CHECK(!parse(alone, 2, &options));
    CHECK(!parse(with_now, 4, &options));
    CHECK(!parse(too_far, 4, &options));
    CHECK(!parse(not_a_time, 2, &options));
}

static void a_notice_is_read_as_a_title_and_its_text(void)
{
    char *given[] = {"--notice", "USB DAC", "--notice-text", "Choose the player"};
    SimOptions options;
    CHECK(parse(given, 4, &options));
    CHECK(options.notice_title == given[1]);
    CHECK(options.notice_text == given[3]);
}

static void a_notice_needs_both_its_title_and_its_text(void)
{
    char *title_only[] = {"--notice", "USB DAC"};
    char *text_only[] = {"--notice-text", "Choose the player"};
    SimOptions options;
    CHECK(!parse(title_only, 2, &options));
    CHECK(!parse(text_only, 2, &options));
    CHECK(parse(NULL, 0, &options));
    CHECK(options.notice_title == NULL && options.notice_text == NULL);
}

static void a_battery_level_above_full_is_rejected(void)
{
    char *given[] = {"--battery", "101"};
    SimOptions options;
    CHECK(!parse(given, 2, &options));
}

static void storage_directories_are_read(void)
{
    char *given[] = {"--card", "/music", "--system-dir", "/tmp/echo"};
    SimOptions options;
    CHECK(parse(given, 4, &options));
    CHECK(options.card_directory == given[1]);
    CHECK(options.system_directory == given[3]);
}

static void a_key_script_is_read(void)
{
    char *given[] = {"--keys", "down,play"};
    SimOptions options;
    CHECK(parse(given, 2, &options));
    CHECK(options.key_script == given[1]);
}

static void every_option_is_read(void)
{
    char *given[] = {"--scale", "2", "--frames", "10", "--headless", "--screenshot", "a.bmp"};
    SimOptions options;
    CHECK(parse(given, 7, &options));
    CHECK_EQUAL(2, options.scale);
    CHECK_EQUAL(10, options.frame_limit);
    CHECK(options.headless);
    CHECK(options.screenshot_path == given[6]);
}

static void an_unknown_option_is_rejected(void)
{
    char *given[] = {"--fast"};
    SimOptions options;
    CHECK(!parse(given, 1, &options));
}

static void an_option_missing_its_value_is_rejected(void)
{
    char *given[] = {"--frames"};
    SimOptions options;
    CHECK(!parse(given, 1, &options));
}

static void non_numeric_values_are_rejected(void)
{
    char *given[] = {"--frames", "ten"};
    SimOptions options;
    CHECK(!parse(given, 2, &options));
}

static void a_scale_outside_the_supported_range_is_rejected(void)
{
    char *too_small[] = {"--scale", "0"};
    char *too_large[] = {"--scale", "9"};
    SimOptions options;
    CHECK(!parse(too_small, 2, &options));
    CHECK(!parse(too_large, 2, &options));
}

static void a_headless_run_needs_a_frame_limit(void)
{
    char *given[] = {"--headless"};
    SimOptions options;
    CHECK(!parse(given, 1, &options));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(no_arguments_give_a_windowed_run_at_default_scale),
        TEST_CASE(every_option_is_read),
        TEST_CASE(a_key_script_is_read),
        TEST_CASE(storage_directories_are_read),
        TEST_CASE(the_battery_level_and_charging_are_read),
        TEST_CASE(the_clock_is_unset_unless_asked_for),
        TEST_CASE(the_clock_reads_now_or_utc_seconds_with_an_offset),
        TEST_CASE(an_offset_needs_a_fixed_clock_and_stays_within_a_day),
        TEST_CASE(a_notice_is_read_as_a_title_and_its_text),
        TEST_CASE(a_notice_needs_both_its_title_and_its_text),
        TEST_CASE(a_battery_level_above_full_is_rejected),
        TEST_CASE(an_unknown_option_is_rejected),
        TEST_CASE(an_option_missing_its_value_is_rejected),
        TEST_CASE(non_numeric_values_are_rejected),
        TEST_CASE(a_scale_outside_the_supported_range_is_rejected),
        TEST_CASE(a_headless_run_needs_a_frame_limit),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
