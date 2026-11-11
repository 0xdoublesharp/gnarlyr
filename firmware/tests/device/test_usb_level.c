#include "check.h"
#include "device/usb_level.h"

static void silence_shows_nothing(void)
{
    CHECK_EQUAL(0, usb_level_of(0));
}

static void full_scale_fills_the_meter(void)
{
    CHECK_EQUAL(USB_LEVEL_FULL, usb_level_of(32768));
    CHECK(usb_level_of(32767) >= USB_LEVEL_FULL - 5);
}

static void each_halving_lowers_the_level_by_six_decibels_of_the_scale(void)
{
    int step = USB_LEVEL_FULL * 6 / USB_LEVEL_RANGE_DB;
    int full = usb_level_of(32768);
    int half = usb_level_of(16384);
    int quarter = usb_level_of(8192);
    CHECK(full - half >= step - 2 && full - half <= step + 2);
    CHECK(half - quarter >= step - 2 && half - quarter <= step + 2);
}

static void levels_below_the_range_show_nothing(void)
{
    CHECK_EQUAL(0, usb_level_of(127));
    CHECK_EQUAL(0, usb_level_of(128));
    int step = USB_LEVEL_FULL * 6 / USB_LEVEL_RANGE_DB;
    CHECK(usb_level_of(256) >= step - 2 && usb_level_of(256) <= step + 2);
}

static void the_level_rises_at_once_and_falls_gently(void)
{
    CHECK_EQUAL(200, usb_level_follow(10, 200, 50));
    CHECK_EQUAL(200 - USB_LEVEL_FALL_PER_SECOND / 10, usb_level_follow(200, 0, 100));
    CHECK_EQUAL(120, usb_level_follow(200, 120, 1000));
    CHECK_EQUAL(0, usb_level_follow(5, 0, 1000));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(silence_shows_nothing),
        TEST_CASE(full_scale_fills_the_meter),
        TEST_CASE(each_halving_lowers_the_level_by_six_decibels_of_the_scale),
        TEST_CASE(levels_below_the_range_show_nothing),
        TEST_CASE(the_level_rises_at_once_and_falls_gently),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
