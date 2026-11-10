#include "check.h"
#include "device/usb_forced.h"

static UsbForced forced;

static bool requested_at(uint32_t now_ms, uint32_t commands)
{
    return usb_forced_connected(&forced, (UsbForcedNow){false, true, commands, now_ms});
}

static bool cable_at(uint32_t now_ms, bool cable)
{
    return usb_forced_connected(&forced, (UsbForcedNow){cable, false, 0, now_ms});
}

static void a_session_by_the_cable_lasts_as_long_as_the_cable(void)
{
    forced = (UsbForced){.cable_seen = false};
    CHECK(cable_at(1000, true));
    CHECK(cable_at(900000, true));
    CHECK(!cable_at(900001, false));
}

static void without_a_cable_or_a_request_there_is_no_session(void)
{
    forced = (UsbForced){.cable_seen = false};
    CHECK(!cable_at(1000, false));
}

static void a_requested_session_holds_without_the_cable_bit_at_any_millisecond(void)
{
    for (uint32_t start_ms = 5050; start_ms < 5058; start_ms++) {
        forced = (UsbForced){.cable_seen = false};
        CHECK(requested_at(start_ms, 0));
        CHECK(requested_at(start_ms + 1, 0));
    }
    forced = (UsbForced){.cable_seen = false};
    CHECK(requested_at(0, 0));
    CHECK(requested_at(1, 0));
}

static void a_requested_session_ends_after_a_minute_without_a_command(void)
{
    forced = (UsbForced){.cable_seen = false};
    CHECK(requested_at(5054, 0));
    CHECK(requested_at(5054 + USB_FORCED_IDLE_MS - 1, 0));
    CHECK(!requested_at(5054 + USB_FORCED_IDLE_MS, 0));
}

static void every_command_from_the_host_starts_the_minute_again(void)
{
    forced = (UsbForced){.cable_seen = false};
    CHECK(requested_at(1000, 0));
    CHECK(requested_at(50000, 7));
    CHECK(requested_at(50000 + USB_FORCED_IDLE_MS - 1, 7));
    CHECK(requested_at(50000 + USB_FORCED_IDLE_MS - 1, 8));
    CHECK(requested_at(100000 + USB_FORCED_IDLE_MS, 8));
    CHECK(!requested_at(50000 + 2 * USB_FORCED_IDLE_MS, 8));
}

static void the_minute_is_counted_across_a_wrap_of_the_clock(void)
{
    forced = (UsbForced){.cable_seen = false};
    CHECK(requested_at(0xFFFFFF00u, 0));
    CHECK(requested_at(0x00000100u, 0));
    CHECK(!requested_at(0xFFFFFF00u + USB_FORCED_IDLE_MS, 0));
}

static void once_the_cable_bit_was_seen_its_loss_ends_a_requested_session(void)
{
    forced = (UsbForced){.cable_seen = false};
    CHECK(usb_forced_connected(&forced, (UsbForcedNow){true, true, 0, 1000}));
    CHECK(!requested_at(1001, 0));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_session_by_the_cable_lasts_as_long_as_the_cable),
        TEST_CASE(without_a_cable_or_a_request_there_is_no_session),
        TEST_CASE(a_requested_session_holds_without_the_cable_bit_at_any_millisecond),
        TEST_CASE(a_requested_session_ends_after_a_minute_without_a_command),
        TEST_CASE(every_command_from_the_host_starts_the_minute_again),
        TEST_CASE(the_minute_is_counted_across_a_wrap_of_the_clock),
        TEST_CASE(once_the_cable_bit_was_seen_its_loss_ends_a_requested_session),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
