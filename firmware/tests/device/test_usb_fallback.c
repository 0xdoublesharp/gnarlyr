#include "check.h"
#include "device/usb_fallback.h"

static const UsbFallbackSigns SILENT = {.configured = false, .commands = 0};
static const UsbFallbackSigns CONFIGURED_ONLY = {.configured = true, .commands = 1};
static const UsbFallbackSigns WORKING = {.configured = true, .commands = USB_FALLBACK_COMMANDS};

static UsbFallback enumerated_at(uint32_t now_ms)
{
    UsbFallback fallback;
    usb_fallback_start(&fallback, true);
    usb_fallback_enumerated(&fallback, now_ms);
    return fallback;
}

static void a_high_speed_session_that_never_works_falls_back_ten_seconds_after_enumeration(void)
{
    UsbFallback fallback = enumerated_at(1000);
    CHECK(!usb_fallback_due(&fallback, SILENT, 1000 + USB_FALLBACK_MS - 1));
    CHECK(usb_fallback_due(&fallback, SILENT, 1000 + USB_FALLBACK_MS));
}

static void the_time_before_enumeration_does_not_count(void)
{
    UsbFallback fallback;
    usb_fallback_start(&fallback, true);
    CHECK(!usb_fallback_due(&fallback, SILENT, 3 * USB_FALLBACK_MS));
    usb_fallback_enumerated(&fallback, 3 * USB_FALLBACK_MS);
    CHECK(!usb_fallback_due(&fallback, SILENT, 4 * USB_FALLBACK_MS - 1));
    CHECK(usb_fallback_due(&fallback, SILENT, 4 * USB_FALLBACK_MS));
}

static void a_later_enumeration_does_not_restart_the_timeout(void)
{
    UsbFallback fallback = enumerated_at(0);
    usb_fallback_enumerated(&fallback, USB_FALLBACK_MS - 1);
    CHECK(usb_fallback_due(&fallback, SILENT, USB_FALLBACK_MS));
}

static void it_falls_back_only_once(void)
{
    UsbFallback fallback = enumerated_at(0);
    CHECK(usb_fallback_due(&fallback, SILENT, USB_FALLBACK_MS));
    CHECK(!usb_fallback_due(&fallback, SILENT, USB_FALLBACK_MS + 1));
    CHECK(!usb_fallback_due(&fallback, SILENT, 3 * USB_FALLBACK_MS));
}

static void a_working_session_never_falls_back(void)
{
    UsbFallback fallback = enumerated_at(0);
    CHECK(!usb_fallback_due(&fallback, WORKING, 500));
    CHECK(!usb_fallback_due(&fallback, SILENT, 2 * USB_FALLBACK_MS));
    CHECK_EQUAL(USB_FALLBACK_NONE, fallback.cause);
}

static void a_full_speed_session_never_falls_back(void)
{
    UsbFallback fallback;
    usb_fallback_start(&fallback, false);
    usb_fallback_enumerated(&fallback, 0);
    CHECK(!usb_fallback_due(&fallback, SILENT, 5 * USB_FALLBACK_MS));
}

static void it_records_whether_the_host_configured_the_device(void)
{
    UsbFallback silent = enumerated_at(0);
    CHECK(usb_fallback_due(&silent, SILENT, USB_FALLBACK_MS));
    CHECK_EQUAL(USB_FALLBACK_NOT_CONFIGURED, silent.cause);
    UsbFallback quiet = enumerated_at(0);
    CHECK(usb_fallback_due(&quiet, CONFIGURED_ONLY, USB_FALLBACK_MS));
    CHECK_EQUAL(USB_FALLBACK_FEW_COMMANDS, quiet.cause);
    CHECK_EQUAL(1, quiet.commands);
}

static void the_timeout_survives_the_clock_wrapping(void)
{
    UsbFallback fallback = enumerated_at(UINT32_MAX - 100);
    CHECK(!usb_fallback_due(&fallback, SILENT, 200));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_high_speed_session_that_never_works_falls_back_ten_seconds_after_enumeration),
        TEST_CASE(the_time_before_enumeration_does_not_count),
        TEST_CASE(a_later_enumeration_does_not_restart_the_timeout),
        TEST_CASE(it_falls_back_only_once),
        TEST_CASE(a_working_session_never_falls_back),
        TEST_CASE(a_full_speed_session_never_falls_back),
        TEST_CASE(it_records_whether_the_host_configured_the_device),
        TEST_CASE(the_timeout_survives_the_clock_wrapping),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
