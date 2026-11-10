#include "check.h"
#include "device/usb_plug.h"

static UsbPlug plug;

static UsbPlugAction at(uint32_t now_ms, bool cable)
{
    return usb_plug_update(&plug, cable, now_ms);
}

static void listening_from(uint32_t plugged_ms)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    at(plugged_ms, true);
    at(plugged_ms + USB_PLUG_STEADY_MS, true);
    usb_plug_started(&plug, true);
}

static void nothing_happens_without_a_cable(void)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    CHECK_EQUAL(USB_PLUG_NOTHING, at(0, false));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(60000, false));
    CHECK_EQUAL(USB_PLUG_UNPLUGGED, plug.state);
}

static void the_cable_must_hold_for_a_second_before_usb_is_started(void)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5000, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5500, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5999, true));
    CHECK_EQUAL(USB_PLUG_START, at(6000, true));
}

static void a_cable_that_bounces_starts_its_second_over(void)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    at(5000, true);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5900, false));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5950, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(6100, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(6949, true));
    CHECK_EQUAL(USB_PLUG_START, at(6950, true));
}

static void the_second_is_counted_across_a_wrap_of_the_clock(void)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    at(0xFFFFFE00u, true);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(0xFFFFFFFFu, true));
    CHECK_EQUAL(USB_PLUG_START, at(0x1E8u, true));
}

static void usb_keeps_listening_for_as_long_as_the_cable_is_in(void)
{
    listening_from(1000);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5000, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(3600000, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(0x80000000u, true));
    CHECK_EQUAL(USB_PLUG_LISTENING, plug.state);
}

static void pulling_the_cable_while_listening_stops_usb(void)
{
    listening_from(1000);
    CHECK_EQUAL(USB_PLUG_STOP, at(900000, false));
    CHECK_EQUAL(USB_PLUG_UNPLUGGED, plug.state);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(900001, false));
}

static void after_a_session_nothing_starts_until_the_cable_has_left(void)
{
    listening_from(1000);
    usb_plug_served(&plug);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(9000, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(900000, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(900001, false));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(900002, true));
    CHECK_EQUAL(USB_PLUG_START, at(900002 + USB_PLUG_STEADY_MS, true));
}

static void a_start_that_fails_is_not_tried_again_until_the_cable_has_left(void)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    at(0, true);
    CHECK_EQUAL(USB_PLUG_START, at(USB_PLUG_STEADY_MS, true));
    usb_plug_started(&plug, false);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(5000, true));
    CHECK_EQUAL(USB_PLUG_SERVED, plug.state);
}

static void a_retry_listens_again_with_the_cable_still_in(void)
{
    listening_from(1000);
    usb_plug_served(&plug);
    usb_plug_retry(&plug);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(20000, true));
    CHECK_EQUAL(USB_PLUG_START, at(20000 + USB_PLUG_STEADY_MS, true));
}

static void a_retry_does_not_restart_a_second_already_being_counted(void)
{
    plug = (UsbPlug){.state = USB_PLUG_UNPLUGGED};
    at(1000, true);
    usb_plug_retry(&plug);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(1999, true));
    CHECK_EQUAL(USB_PLUG_START, at(2000, true));
}

static void a_retry_while_listening_changes_nothing(void)
{
    listening_from(1000);
    usb_plug_retry(&plug);
    CHECK_EQUAL(USB_PLUG_LISTENING, plug.state);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(20000, true));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(nothing_happens_without_a_cable),
        TEST_CASE(the_cable_must_hold_for_a_second_before_usb_is_started),
        TEST_CASE(a_cable_that_bounces_starts_its_second_over),
        TEST_CASE(the_second_is_counted_across_a_wrap_of_the_clock),
        TEST_CASE(usb_keeps_listening_for_as_long_as_the_cable_is_in),
        TEST_CASE(pulling_the_cable_while_listening_stops_usb),
        TEST_CASE(after_a_session_nothing_starts_until_the_cable_has_left),
        TEST_CASE(a_start_that_fails_is_not_tried_again_until_the_cable_has_left),
        TEST_CASE(a_retry_listens_again_with_the_cable_still_in),
        TEST_CASE(a_retry_does_not_restart_a_second_already_being_counted),
        TEST_CASE(a_retry_while_listening_changes_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
