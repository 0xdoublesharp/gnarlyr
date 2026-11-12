#include "check.h"
#include "screens/usb_meter.h"

enum {
    MIB_SECTORS = 2048,
    START_MS = 1000
};

static UsbMeter meter;
static uint32_t totals[USB_METER_DIRECTIONS];

static void step(uint32_t read, uint32_t written, uint32_t now_ms)
{
    totals[USB_METER_READ] += read;
    totals[USB_METER_WRITE] += written;
    usb_meter_sample(&meter, totals, now_ms);
}

static void started(void)
{
    totals[USB_METER_READ] = 0;
    totals[USB_METER_WRITE] = 0;
    usb_meter_start(&meter, START_MS);
}

static void a_rate_is_bytes_per_millisecond_in_kilobytes_a_second(void)
{
    started();
    step(MIB_SECTORS, MIB_SECTORS / 2, START_MS + 500);
    CHECK_EQUAL(2097, usb_meter_rate(&meter, USB_METER_READ, 0));
    CHECK_EQUAL(1048, usb_meter_rate(&meter, USB_METER_WRITE, 0));
    CHECK_EQUAL(3145, usb_meter_total(&meter, 0));
}

static void a_sample_is_due_every_half_second(void)
{
    started();
    CHECK(!usb_meter_due(&meter, START_MS + 499));
    CHECK(usb_meter_due(&meter, START_MS + 500));
    step(1, 0, START_MS + 500);
    CHECK(!usb_meter_due(&meter, START_MS + 999));
}

static void the_history_keeps_the_last_twenty_samples_newest_first(void)
{
    started();
    for (uint32_t index = 1; index <= 25; index++) {
        step(index * 100, 0, START_MS + index * 512);
    }
    CHECK_EQUAL(2500, usb_meter_rate(&meter, USB_METER_READ, 0));
    CHECK_EQUAL(2400, usb_meter_rate(&meter, USB_METER_READ, 1));
    CHECK_EQUAL(600, usb_meter_rate(&meter, USB_METER_READ, USB_METER_SAMPLES - 1));
    CHECK_EQUAL(0, usb_meter_rate(&meter, USB_METER_READ, USB_METER_SAMPLES));
    CHECK_EQUAL(0, usb_meter_rate(&meter, USB_METER_READ, -1));
}

static void a_young_history_reads_zero_beyond_its_samples(void)
{
    started();
    step(10, 0, START_MS + 500);
    CHECK(usb_meter_rate(&meter, USB_METER_READ, 0) > 0);
    CHECK_EQUAL(0, usb_meter_rate(&meter, USB_METER_READ, 1));
}

static void the_meter_rests_once_idle_samples_fill_the_history(void)
{
    started();
    CHECK(!usb_meter_moving(&meter));
    step(MIB_SECTORS, 0, START_MS + 500);
    CHECK(usb_meter_moving(&meter));
    for (uint32_t index = 2; index <= USB_METER_SAMPLES; index++) {
        step(0, 0, START_MS + index * 500);
        CHECK(usb_meter_moving(&meter));
    }
    step(0, 0, START_MS + (USB_METER_SAMPLES + 1) * 500);
    CHECK(!usb_meter_moving(&meter));
}

static void peak_and_average_count_only_busy_time(void)
{
    started();
    step(MIB_SECTORS, 0, START_MS + 500);
    step(0, 0, START_MS + 10500);
    step(0, MIB_SECTORS * 2, START_MS + 11000);
    CHECK_EQUAL(4194, meter.peak);
    CHECK_EQUAL(3145, usb_meter_average(&meter));
    CHECK_EQUAL(1000, meter.active_ms);
}

static void a_fast_link_saturates_instead_of_wrapping(void)
{
    started();
    step(UINT32_MAX / 2, 0, START_MS + 1);
    CHECK_EQUAL(USB_METER_RATE_LIMIT, usb_meter_rate(&meter, USB_METER_READ, 0));
}

static void a_sample_at_the_same_moment_is_ignored(void)
{
    started();
    step(MIB_SECTORS, 0, START_MS);
    CHECK_EQUAL(0, meter.count);
    CHECK_EQUAL(0, usb_meter_average(&meter));
}

static void a_light_fades_over_three_hundred_milliseconds(void)
{
    CHECK_EQUAL(0, usb_meter_glow(false, 0));
    CHECK_EQUAL(USB_METER_GLOW_FULL, usb_meter_glow(true, 0));
    CHECK_EQUAL(128, usb_meter_glow(true, 150));
    CHECK(usb_meter_glow(true, 299) > 0);
    CHECK_EQUAL(0, usb_meter_glow(true, USB_METER_GLOW_MS));
}

static void commands_closer_than_the_fade_keep_a_light_lit(void)
{
    static const uint32_t COMMANDS_MS[] = {0, 40, 230, 260, 480, 700, 760, 1000};
    uint32_t last = 0;
    int next = 0;
    for (uint32_t now = 0; now <= 1000; now += 10) {
        while (next < 8 && COMMANDS_MS[next] <= now) {
            last = COMMANDS_MS[next++];
        }
        CHECK(usb_meter_glow(true, now - last) > 0);
    }
}

static void a_steady_state_holds_for_a_second(void)
{
    UsbSteady steady;
    usb_steady_start(&steady, 0, START_MS);
    CHECK(usb_steady_follow(&steady, 1, START_MS));
    CHECK_EQUAL(1, steady.shown);
    CHECK(!usb_steady_follow(&steady, 0, START_MS + 500));
    CHECK(!usb_steady_follow(&steady, 0, START_MS + USB_METER_HOLD_MS - 1));
    CHECK_EQUAL(1, steady.shown);
    CHECK(usb_steady_follow(&steady, 0, START_MS + USB_METER_HOLD_MS));
    CHECK_EQUAL(0, steady.shown);
}

static void a_flickering_wish_changes_the_shown_state_at_most_once_a_second(void)
{
    UsbSteady steady;
    usb_steady_start(&steady, 0, START_MS);
    uint32_t changed_at = 0;
    int changes = 0;
    for (uint32_t now = START_MS; now < START_MS + 5000; now += 100) {
        if (usb_steady_follow(&steady, (uint8_t)((now / 100) % 2), now)) {
            CHECK(changes == 0 || now - changed_at >= USB_METER_HOLD_MS);
            changed_at = now;
            changes++;
        }
    }
    CHECK(changes <= 5);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_rate_is_bytes_per_millisecond_in_kilobytes_a_second),
        TEST_CASE(a_sample_is_due_every_half_second),
        TEST_CASE(the_history_keeps_the_last_twenty_samples_newest_first),
        TEST_CASE(a_young_history_reads_zero_beyond_its_samples),
        TEST_CASE(the_meter_rests_once_idle_samples_fill_the_history),
        TEST_CASE(peak_and_average_count_only_busy_time),
        TEST_CASE(a_fast_link_saturates_instead_of_wrapping),
        TEST_CASE(a_sample_at_the_same_moment_is_ignored),
        TEST_CASE(a_light_fades_over_three_hundred_milliseconds),
        TEST_CASE(commands_closer_than_the_fade_keep_a_light_lit),
        TEST_CASE(a_steady_state_holds_for_a_second),
        TEST_CASE(a_flickering_wish_changes_the_shown_state_at_most_once_a_second),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
