#include "check.h"
#include "device/usb_activity.h"

static UsbActivity activity;

static void follow(uint32_t read, uint32_t written, uint32_t flushes, uint32_t now_ms)
{
    MscStats stats = {.sectors_read = read, .sectors_written = written, .flushes = flushes};
    usb_activity_follow(&activity, &stats, now_ms);
}

static void a_session_without_traffic_is_idle_and_quiet(void)
{
    usb_activity_start(&activity, 5000);
    follow(0, 0, 0, 5000);
    CHECK_EQUAL(USB_ACTIVITY_IDLE, usb_activity_now(&activity, 5000));
    CHECK(usb_activity_quiet(&activity, 5000));
}

static void sectors_read_show_reading_until_the_show_time_passes(void)
{
    usb_activity_start(&activity, 0);
    follow(8, 0, 0, 100);
    CHECK_EQUAL(USB_ACTIVITY_READING, usb_activity_now(&activity, 100));
    CHECK_EQUAL(USB_ACTIVITY_READING, usb_activity_now(&activity, 100 + USB_ACTIVITY_SHOW_MS - 1));
    CHECK_EQUAL(USB_ACTIVITY_IDLE, usb_activity_now(&activity, 100 + USB_ACTIVITY_SHOW_MS));
}

static void reading_never_breaks_the_quiet(void)
{
    usb_activity_start(&activity, 0);
    follow(8, 0, 0, 100);
    CHECK(usb_activity_quiet(&activity, 100));
}

static void writing_wins_over_reading(void)
{
    usb_activity_start(&activity, 0);
    follow(8, 4, 0, 100);
    CHECK_EQUAL(USB_ACTIVITY_WRITING, usb_activity_now(&activity, 100));
}

static void the_host_is_quiet_only_after_the_quiet_period_without_writes(void)
{
    usb_activity_start(&activity, 0);
    follow(0, 4, 0, 1000);
    CHECK(!usb_activity_quiet(&activity, 1000));
    CHECK(!usb_activity_quiet(&activity, 1000 + USB_ACTIVITY_QUIET_MS - 1));
    CHECK(usb_activity_quiet(&activity, 1000 + USB_ACTIVITY_QUIET_MS));
}

static void a_later_write_starts_the_quiet_period_again(void)
{
    usb_activity_start(&activity, 0);
    follow(0, 4, 0, 1000);
    follow(0, 4, 0, 2000);
    follow(0, 5, 0, 3000);
    CHECK(!usb_activity_quiet(&activity, 1000 + USB_ACTIVITY_QUIET_MS));
    CHECK(usb_activity_quiet(&activity, 3000 + USB_ACTIVITY_QUIET_MS));
}

static void a_cache_flush_counts_as_writing(void)
{
    usb_activity_start(&activity, 0);
    follow(0, 0, 1, 1000);
    CHECK_EQUAL(USB_ACTIVITY_WRITING, usb_activity_now(&activity, 1000));
    CHECK(!usb_activity_quiet(&activity, 1000 + USB_ACTIVITY_QUIET_MS - 1));
}

static void timing_survives_the_millisecond_counter_wrapping(void)
{
    uint32_t start = UINT32_MAX - 500;
    usb_activity_start(&activity, start);
    follow(0, 4, 0, start + 100);
    CHECK(!usb_activity_quiet(&activity, start + 100 + USB_ACTIVITY_QUIET_MS - 1));
    CHECK(usb_activity_quiet(&activity, start + 100 + USB_ACTIVITY_QUIET_MS));
}

static void bytes_written_are_counted_from_the_start_in_kibibytes(void)
{
    usb_activity_start(&activity, 0);
    follow(0, 6, 0, 10);
    follow(0, 2048, 0, 20);
    CHECK_EQUAL(1024, usb_activity_written_kib(&activity));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_session_without_traffic_is_idle_and_quiet),
        TEST_CASE(sectors_read_show_reading_until_the_show_time_passes),
        TEST_CASE(reading_never_breaks_the_quiet),
        TEST_CASE(writing_wins_over_reading),
        TEST_CASE(the_host_is_quiet_only_after_the_quiet_period_without_writes),
        TEST_CASE(a_later_write_starts_the_quiet_period_again),
        TEST_CASE(a_cache_flush_counts_as_writing),
        TEST_CASE(timing_survives_the_millisecond_counter_wrapping),
        TEST_CASE(bytes_written_are_counted_from_the_start_in_kibibytes),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
