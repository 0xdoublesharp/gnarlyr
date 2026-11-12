#include <string.h>

#include "app/app.h"
#include "check.h"
#include "fake_hal.h"
#include "text_spy.h"

static bool shown(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strstr(text_spy_entry(index)->shown, text) != NULL) {
            return true;
        }
    }
    return false;
}

static void dark_charging_collects_samples_and_wakes_with_a_stable_estimate(void)
{
    fake_hal_reset();
    HalBattery battery = {.known = true, .percent = 50, .usb_power = true, .charging = true};
    fake_hal_set_battery(battery);
    fake_hal_set_usb_start(true);
    fake_hal_set_charging_host(true);
    fake_hal_set_milliseconds(1000);
    app_start();
    CHECK(app_charging());
    app_step();
    for (uint32_t pass = 0; pass < 64 && app_preparing(); pass++) {
        fake_hal_set_milliseconds(1001 + pass);
        app_step();
    }
    CHECK(!app_preparing());
    fake_hal_set_milliseconds(8000);
    app_step();
    fake_hal_set_milliseconds(9000);
    app_step();
    CHECK(!fake_hal_display_on());
    int frames = fake_hal_present_count();
    for (uint32_t minute = 1; minute <= 20; minute++) {
        battery.percent = (uint8_t)(50 + minute * 3 / 10);
        fake_hal_set_battery(battery);
        fake_hal_set_milliseconds(1000 + minute * 60000);
        app_step();
        CHECK(!fake_hal_display_on());
        CHECK_EQUAL(frames, fake_hal_present_count());
    }
    text_spy_reset();
    app_wake();
    fake_hal_set_milliseconds(1201001);
    app_step();
    CHECK(fake_hal_display_on());
    CHECK(shown("Computer +18%/h"));
    CHECK(shown("Full in ~"));
    fake_hal_set_charging_host(false);
    fake_hal_set_milliseconds(1203001);
    text_spy_reset();
    app_step();
    CHECK(shown("Charger Measuring"));
    CHECK(!shown("+18%/h"));
    app_stop();
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(dark_charging_collects_samples_and_wakes_with_a_stable_estimate),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
