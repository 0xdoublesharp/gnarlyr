#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "screens/charge_screen.h"
#include "text_spy.h"
#include "ui/label.h"

static void usb_power_does_not_mean_the_battery_is_full(void)
{
    char text[64];
    charge_screen_text((HalBattery){.known = true, .percent = 64, .usb_power = true}, text,
                       sizeof text);
    CHECK(strcmp(text, i18n_text(STR_CHARGED)) != 0);
}

static void an_unknown_battery_is_never_reported_full(void)
{
    char text[64];
    charge_screen_text((HalBattery){.usb_power = true}, text, sizeof text);
    CHECK(strcmp(text, i18n_text(STR_CHARGED)) != 0);
}

static void battery_power_is_never_reported_charging(void)
{
    char text[64];
    charge_screen_text((HalBattery){.known = true, .percent = 64}, text, sizeof text);
    CHECK(strstr(text, i18n_text(STR_CHARGING)) == NULL);
}

static void a_known_full_battery_on_usb_is_reported_charged(void)
{
    char text[64];
    charge_screen_text((HalBattery){.known = true, .percent = 100, .usb_power = true}, text,
                       sizeof text);
    CHECK(strcmp(text, i18n_text(STR_CHARGED)) == 0);
}

static Color pixels[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->shown, text) == 0) {
            return true;
        }
    }
    return false;
}

static void only_visible_battery_changes_require_a_usb_redraw(void)
{
    HalBattery before = {.known = true, .percent = 64, .usb_power = true, .millivolts = 4300};
    HalBattery after = before;
    after.millivolts = 4800;
    CHECK(!app_usb_battery_changed(before, after));
    after.percent++;
    CHECK(app_usb_battery_changed(before, after));
    after = before;
    after.charging = true;
    CHECK(app_usb_battery_changed(before, after));
    after = before;
    after.known = false;
    CHECK(app_usb_battery_changed(before, after));
    after = before;
    after.usb_power = false;
    CHECK(app_usb_battery_changed(before, after));
}

static void rect_fits(Rect area, const Theme *theme)
{
    CHECK(area.width > 0);
    CHECK(area.height > 0);
    CHECK(area.x >= 0);
    CHECK(area.y >= 0);
    CHECK(area.x + area.width <= theme->metrics.screen_width);
    CHECK(area.y + area.height <= theme->metrics.screen_height);
}

static void charge_and_usb_details_fit_every_size_and_orientation(void)
{
    static const StringId details[] = {STR_HOLD_POWER, STR_USB_STARTING, STR_USB_EJECT_DRIVE,
                                       STR_USB_EJECT_DRIVES, STR_USB_DAC_FAILED};
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            for (size_t index = 0; index < sizeof details / sizeof details[0]; index++) {
                const char *detail = i18n_text(details[index]);
                ChargeScreenLayout layout = charge_screen_layout(&theme, detail);
                rect_fits(layout.header, &theme);
                rect_fits(layout.battery, &theme);
                rect_fits(layout.percent, &theme);
                rect_fits(layout.status, &theme);
                rect_fits(layout.voltage, &theme);
                rect_fits(layout.detail, &theme);
                CHECK(layout.battery.y >= layout.percent.y + layout.percent.height);
                CHECK(layout.voltage.y + layout.voltage.height <=
                      layout.detail.y - theme.metrics.padding);
                CHECK(layout.status.height >= font_line_height(theme.fonts.title));
                CHECK(layout.detail.height >=
                      label_wrap_count(theme.fonts.small, detail, layout.detail.width) *
                          font_line_height(theme.fonts.small));
            }
        }
    }
}

static void usb_voltage_is_hidden_and_the_real_mode_and_charge_are_visible(void)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    text_spy_reset();
    usb_screen_draw(
        &canvas, &theme,
        (HalBattery){
            .known = true, .percent = 64, .charging = true, .usb_power = true, .millivolts = 4800},
        (AppUsbStatus){.view = APP_USB_STORAGE,
                       .information = i18n_text(STR_POWER_INTERNAL_CARD),
                       .detail = i18n_text(STR_USB_EJECT_DRIVES)});
    CHECK(saw(i18n_text(STR_POWER_STORAGE)));
    CHECK(saw(i18n_text(STR_CHARGING)));
    CHECK(saw(i18n_text(STR_POWER_INTERNAL_CARD)));
    CHECK(saw("64%"));
    CHECK(!saw("4800 mV"));
}

static void an_unknown_battery_has_no_invented_percent_or_voltage(void)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    text_spy_reset();
    charge_screen_draw(&canvas, &theme, (HalBattery){.usb_power = true});
    CHECK(saw(i18n_text(STR_POWER_UNKNOWN)));
    CHECK(saw(i18n_text(STR_POWER_USB)));
    CHECK(!saw("0%"));
    CHECK(!saw("0 mV"));
    CHECK(!saw(i18n_text(STR_CHARGED)));
}

int main(void)
{
    fake_hal_reset();
    i18n_detach();
    static const TestCase cases[] = {
        TEST_CASE(only_visible_battery_changes_require_a_usb_redraw),
        TEST_CASE(charge_and_usb_details_fit_every_size_and_orientation),
        TEST_CASE(usb_voltage_is_hidden_and_the_real_mode_and_charge_are_visible),
        TEST_CASE(an_unknown_battery_has_no_invented_percent_or_voltage),
        TEST_CASE(usb_power_does_not_mean_the_battery_is_full),
        TEST_CASE(an_unknown_battery_is_never_reported_full),
        TEST_CASE(battery_power_is_never_reported_charging),
        TEST_CASE(a_known_full_battery_on_usb_is_reported_charged),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
