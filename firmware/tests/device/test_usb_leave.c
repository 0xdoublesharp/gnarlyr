#include "check.h"
#include "device/usb_leave.h"

static UsbLeave leave;

static UsbLeaveState step(uint32_t now_ms, bool quiet)
{
    return usb_leave_step(&leave, (UsbLeaveNow){.now_ms = now_ms, .quiet = quiet});
}

static void a_short_press_of_back_keeps_serving(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 1000);
    CHECK_EQUAL(USB_LEAVE_SERVING, step(1000 + USB_LEAVE_HOLD_MS - 1, true));
    usb_leave_key(&leave, false, 1000 + USB_LEAVE_HOLD_MS - 1);
    CHECK_EQUAL(USB_LEAVE_SERVING, step(9000, true));
}

static void holding_back_while_the_host_is_quiet_removes_the_media(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 1000);
    CHECK_EQUAL(USB_LEAVE_REMOVING, step(1000 + USB_LEAVE_HOLD_MS, true));
}

static void after_the_grace_period_the_session_ends_as_an_eject(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    CHECK_EQUAL(USB_LEAVE_REMOVING, step(USB_LEAVE_HOLD_MS, true));
    CHECK_EQUAL(USB_LEAVE_REMOVING, step(USB_LEAVE_HOLD_MS + USB_LEAVE_GRACE_MS - 1, true));
    CHECK_EQUAL(USB_LEAVE_EJECTED, step(USB_LEAVE_HOLD_MS + USB_LEAVE_GRACE_MS, true));
}

static void a_host_that_keeps_reading_cannot_hold_the_grace_period_open(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    step(USB_LEAVE_HOLD_MS, true);
    usb_leave_key(&leave, false, USB_LEAVE_HOLD_MS + 10);
    CHECK_EQUAL(USB_LEAVE_EJECTED, step(USB_LEAVE_HOLD_MS + USB_LEAVE_GRACE_MS, false));
}

static void holding_back_while_the_host_writes_waits_for_it_to_finish(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    CHECK_EQUAL(USB_LEAVE_WAITING, step(USB_LEAVE_HOLD_MS, false));
    usb_leave_key(&leave, false, USB_LEAVE_HOLD_MS + 100);
    CHECK_EQUAL(USB_LEAVE_WAITING, step(60000, false));
    CHECK_EQUAL(USB_LEAVE_REMOVING, step(60001, true));
    CHECK_EQUAL(USB_LEAVE_EJECTED, step(60001 + USB_LEAVE_GRACE_MS, true));
}

static void keeping_back_held_while_waiting_forces_the_disconnect(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    CHECK_EQUAL(USB_LEAVE_WAITING, step(USB_LEAVE_HOLD_MS, false));
    CHECK(usb_leave_holding(&leave));
    CHECK_EQUAL(USB_LEAVE_WAITING, step(USB_LEAVE_HOLD_MS + USB_LEAVE_FORCE_MS - 1, false));
    CHECK_EQUAL(USB_LEAVE_FORCED, step(USB_LEAVE_HOLD_MS + USB_LEAVE_FORCE_MS, false));
}

static void a_new_hold_while_waiting_forces_after_the_force_time(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    step(USB_LEAVE_HOLD_MS, false);
    usb_leave_key(&leave, false, USB_LEAVE_HOLD_MS + 200);
    CHECK(!usb_leave_holding(&leave));
    usb_leave_key(&leave, true, 5000);
    CHECK_EQUAL(USB_LEAVE_WAITING, step(5000 + USB_LEAVE_FORCE_MS - 1, false));
    CHECK_EQUAL(USB_LEAVE_FORCED, step(5000 + USB_LEAVE_FORCE_MS, false));
}

static void a_write_refused_after_the_removal_ends_like_an_unplug(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    step(USB_LEAVE_HOLD_MS, true);
    UsbLeaveNow now = {.now_ms = USB_LEAVE_HOLD_MS + 50, .quiet = true, .refused = true};
    CHECK_EQUAL(USB_LEAVE_FORCED, usb_leave_step(&leave, now));
}

static void a_refused_write_before_any_request_changes_nothing(void)
{
    leave = (UsbLeave){0};
    UsbLeaveNow now = {.now_ms = 50, .quiet = true, .refused = true};
    CHECK_EQUAL(USB_LEAVE_SERVING, usb_leave_step(&leave, now));
}

static void the_end_stays_once_reached(void)
{
    leave = (UsbLeave){0};
    usb_leave_key(&leave, true, 0);
    step(USB_LEAVE_HOLD_MS, true);
    step(USB_LEAVE_HOLD_MS + USB_LEAVE_GRACE_MS, true);
    usb_leave_key(&leave, true, 9000);
    CHECK_EQUAL(USB_LEAVE_EJECTED, step(20000, false));
}

static void the_hold_is_timed_across_the_millisecond_counter_wrapping(void)
{
    leave = (UsbLeave){0};
    uint32_t start = UINT32_MAX - 200;
    usb_leave_key(&leave, true, start);
    CHECK_EQUAL(USB_LEAVE_SERVING, step(start + USB_LEAVE_HOLD_MS - 1, true));
    CHECK_EQUAL(USB_LEAVE_REMOVING, step(start + USB_LEAVE_HOLD_MS, true));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_short_press_of_back_keeps_serving),
        TEST_CASE(holding_back_while_the_host_is_quiet_removes_the_media),
        TEST_CASE(after_the_grace_period_the_session_ends_as_an_eject),
        TEST_CASE(a_host_that_keeps_reading_cannot_hold_the_grace_period_open),
        TEST_CASE(holding_back_while_the_host_writes_waits_for_it_to_finish),
        TEST_CASE(keeping_back_held_while_waiting_forces_the_disconnect),
        TEST_CASE(a_new_hold_while_waiting_forces_after_the_force_time),
        TEST_CASE(a_write_refused_after_the_removal_ends_like_an_unplug),
        TEST_CASE(a_refused_write_before_any_request_changes_nothing),
        TEST_CASE(the_end_stays_once_reached),
        TEST_CASE(the_hold_is_timed_across_the_millisecond_counter_wrapping),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
