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

static void choosing_player_stops_a_listener_until_the_cable_leaves(void)
{
    listening_from(1000);
    CHECK_EQUAL(USB_PLUG_STOP, usb_plug_request(&plug, false));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(10000, true));
    CHECK_EQUAL(USB_PLUG_SERVED, plug.state);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(11000, false));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(12000, true));
    CHECK_EQUAL(USB_PLUG_START, at(13000, true));
}

static void a_mode_request_restarts_a_served_connection_without_unplugging(void)
{
    listening_from(1000);
    usb_plug_served(&plug);
    CHECK_EQUAL(USB_PLUG_NOTHING, usb_plug_request(&plug, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(10000, true));
    CHECK_EQUAL(USB_PLUG_START, at(11000, true));
}

static void a_mode_change_stops_the_old_listener_before_starting_another(void)
{
    listening_from(1000);
    CHECK_EQUAL(USB_PLUG_STOP, usb_plug_request(&plug, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(10000, true));
    CHECK_EQUAL(USB_PLUG_START, at(11000, true));
}

static void an_unplugged_mode_request_does_not_start_a_session(void)
{
    plug = (UsbPlug){0};
    CHECK_EQUAL(USB_PLUG_NOTHING, usb_plug_request(&plug, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(10000, false));
    CHECK_EQUAL(USB_PLUG_UNPLUGGED, plug.state);
}

static void a_plain_cable_asks_once_a_host_is_found_and_charge_only_never_listens(void)
{
    CHECK(usb_plug_listens(USB_PLUG_ASK, false));
    CHECK(usb_plug_asks(USB_PLUG_ASK, false));
    CHECK(!usb_plug_listens(USB_PLUG_CHARGE, false));
    CHECK(usb_plug_listens(USB_PLUG_STORAGE, false));
    CHECK(!usb_plug_asks(USB_PLUG_STORAGE, false));
}

static void a_saved_usb_dac_answer_listens_without_asking(void)
{
    CHECK(usb_plug_listens(USB_PLUG_DAC, false));
    CHECK(!usb_plug_asks(USB_PLUG_DAC, false));
}

static void a_requested_connection_never_asks_and_always_listens(void)
{
    for (int answer = USB_PLUG_ASK; answer <= USB_PLUG_DAC; answer++) {
        CHECK(usb_plug_listens((UsbPlugAnswer)answer, true));
        CHECK(!usb_plug_asks((UsbPlugAnswer)answer, true));
    }
}

static void an_asked_insertion_is_not_asked_again_until_the_cable_is_replugged(void)
{
    listening_from(1000);
    usb_plug_served(&plug);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(30000, true));
    CHECK_EQUAL(USB_PLUG_SERVED, plug.state);
    CHECK_EQUAL(USB_PLUG_NOTHING, at(31000, false));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(32000, true));
    CHECK_EQUAL(USB_PLUG_START, at(32000 + USB_PLUG_STEADY_MS, true));
}

static void choosing_file_transfer_after_asking_listens_again_on_the_same_cable(void)
{
    listening_from(1000);
    usb_plug_served(&plug);
    CHECK_EQUAL(USB_PLUG_NOTHING, usb_plug_request(&plug, true));
    CHECK_EQUAL(USB_PLUG_NOTHING, at(40000, true));
    CHECK_EQUAL(USB_PLUG_START, at(40000 + USB_PLUG_STEADY_MS, true));
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
        TEST_CASE(choosing_player_stops_a_listener_until_the_cable_leaves),
        TEST_CASE(a_mode_request_restarts_a_served_connection_without_unplugging),
        TEST_CASE(a_mode_change_stops_the_old_listener_before_starting_another),
        TEST_CASE(an_unplugged_mode_request_does_not_start_a_session),
        TEST_CASE(a_plain_cable_asks_once_a_host_is_found_and_charge_only_never_listens),
        TEST_CASE(a_saved_usb_dac_answer_listens_without_asking),
        TEST_CASE(a_requested_connection_never_asks_and_always_listens),
        TEST_CASE(an_asked_insertion_is_not_asked_again_until_the_cable_is_replugged),
        TEST_CASE(choosing_file_transfer_after_asking_listens_again_on_the_same_cable),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
