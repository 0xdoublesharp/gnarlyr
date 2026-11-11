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
    static const StringId details[] = {STR_HOLD_POWER,       STR_CHARGE_MODE_HINT,
                                       STR_USB_STARTING,     STR_USB_EJECT_DRIVE,
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

static void battery_percentage_dominates_a_full_width_slim_gauge(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            ChargeScreenLayout layout = charge_screen_layout(&theme, i18n_text(STR_HOLD_POWER));
            CHECK(layout.percent.height >= 2 * font_line_height(theme.fonts.title));
            CHECK_EQUAL(theme.metrics.screen_width - 2 * theme.metrics.padding,
                        layout.battery.width);
            CHECK(layout.battery.height <= 3 * theme.metrics.scrollbar_inset);
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
    char level[32];
    charge_screen_level_text((HalBattery){.known = true, .percent = 64}, level, sizeof level);
    CHECK(strcmp(level, "64%") == 0);
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
    Rect information = charge_screen_art_layout(&theme).voltage;
    bool blank_information = true;
    for (int y = information.y; y < information.y + information.height; y++) {
        for (int x = information.x; x < information.x + information.width; x++) {
            blank_information &=
                pixels[y * theme.metrics.screen_width + x] == theme.colors.background;
        }
    }
    CHECK(blank_information);
    CHECK(!saw("0%"));
    CHECK(!saw("0 mV"));
    CHECK(!saw(i18n_text(STR_CHARGED)));
}

static uint32_t frame_hash(const Theme *theme)
{
    uint32_t hash = 2166136261u;
    for (int index = 0; index < theme->metrics.screen_width * theme->metrics.screen_height;
         index++) {
        hash = (hash ^ pixels[index]) * 16777619u;
    }
    return hash;
}

static uint32_t charging_frame(HalBattery battery, uint32_t now_ms)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    fake_hal_set_milliseconds(now_ms);
    charge_screen_draw(&canvas, &theme, battery);
    return frame_hash(&theme);
}

static void actual_usb_charging_has_brief_clock_driven_energy_motion(void)
{
    HalBattery battery = {.known = true, .percent = 64, .charging = true, .usb_power = true};
    uint32_t first = charging_frame(battery, 0);
    CHECK_EQUAL(first, charging_frame(battery, CHARGE_SCREEN_FRAME_MS - 1));
    CHECK(first != charging_frame(battery, CHARGE_SCREEN_FRAME_MS));
    CHECK_EQUAL(first, charging_frame(battery, CHARGE_SCREEN_FRAME_MS * CHARGE_SCREEN_PHASES));
}

static void charged_battery_and_noncharging_power_states_stay_still(void)
{
    static const HalBattery states[] = {
        {.known = true, .percent = 100, .usb_power = true},
        {.known = true, .percent = 64, .usb_power = true},
        {.known = true, .percent = 64, .charging = true},
        {.usb_power = true},
    };
    for (size_t index = 0; index < sizeof states / sizeof states[0]; index++) {
        CHECK_EQUAL(charging_frame(states[index], 0), charging_frame(states[index], 200));
    }
}

static void storage_and_dac_screens_do_not_animate_with_the_charger(void)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    HalBattery battery = {.known = true, .percent = 64, .charging = true, .usb_power = true};
    static const AppUsbView views[] = {APP_USB_STORAGE, APP_USB_DAC, APP_USB_CONNECTING};
    for (size_t index = 0; index < sizeof views / sizeof views[0]; index++) {
        AppUsbStatus status = {.view = views[index], .detail = i18n_text(STR_USB_EJECT_DRIVES)};
        fake_hal_set_milliseconds(0);
        usb_screen_draw(&canvas, &theme, battery, status);
        uint32_t first = frame_hash(&theme);
        fake_hal_set_milliseconds(200);
        usb_screen_draw(&canvas, &theme, battery, status);
        CHECK_EQUAL(first, frame_hash(&theme));
    }
}

static Theme charge_theme(int size, int orientation)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    return theme;
}

static Canvas charge_canvas(const Theme *theme)
{
    return canvas_make(pixels,
                       (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
}

static void graphical_charge_layout_fits_all_six_variants_without_text_overlap(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = charge_theme(size, orientation);
            ChargeScreenLayout layout = charge_screen_art_layout(&theme);
            rect_fits(layout.header, &theme);
            rect_fits(layout.battery, &theme);
            rect_fits(layout.percent, &theme);
            rect_fits(layout.status, &theme);
            rect_fits(layout.voltage, &theme);
            rect_fits(layout.detail, &theme);
            CHECK(layout.battery.height >= 2 * font_line_height(theme.fonts.small));
            CHECK(layout.header.y + layout.header.height <= layout.battery.y);
            CHECK(layout.percent.y + layout.percent.height <= layout.status.y);
            CHECK(layout.status.y + layout.status.height <= layout.voltage.y);
            CHECK(layout.voltage.y + layout.voltage.height <=
                  layout.detail.y - theme.metrics.padding);
            if (orientation == ORIENTATION_LANDSCAPE) {
                CHECK(layout.battery.x + layout.battery.width <= layout.percent.x);
            } else {
                CHECK(layout.battery.y + layout.battery.height <= layout.percent.y);
            }
        }
    }
}

static void unknown_artwork_never_uses_a_stale_percentage(void)
{
    HalBattery unknown = {.usb_power = true, .charging = true, .percent = 1};
    uint32_t first = charging_frame(unknown, 0);
    unknown.percent = 99;
    CHECK_EQUAL(first, charging_frame(unknown, 0));
    CHECK(first != charging_frame(unknown, CHARGE_SCREEN_FRAME_MS));
    CHECK(!saw("1%"));
    CHECK(!saw("99%"));
}

static void values_above_full_render_as_full_instead_of_overflowing_the_battery(void)
{
    HalBattery full = {.known = true, .percent = 100, .usb_power = true};
    uint32_t first = charging_frame(full, 0);
    full.percent = 255;
    CHECK_EQUAL(first, charging_frame(full, 0));
}

static void charging_artwork_draws_identically_in_display_strips(void)
{
    HalBattery battery = {.known = true, .percent = 64, .usb_power = true, .charging = true};
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = charge_theme(size, orientation);
            Canvas canvas = charge_canvas(&theme);
            charge_screen_draw_at(&canvas, &theme, battery, CHARGE_SCREEN_FRAME_MS);
            uint32_t first = frame_hash(&theme);
            canvas_clear(&canvas, COLOR_RGB(255, 0, 255));
            for (int y = 0; y < theme.metrics.screen_height; y += 23) {
                Canvas strip =
                    canvas_clipped(&canvas, (Rect){0, y, theme.metrics.screen_width, 23});
                charge_screen_draw_at(&strip, &theme, battery, CHARGE_SCREEN_FRAME_MS);
            }
            CHECK_EQUAL(first, frame_hash(&theme));
        }
    }
}

static void explicit_frame_time_keeps_the_picture_independent_of_clock_reads(void)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = charge_canvas(&theme);
    HalBattery battery = {.known = true, .percent = 64, .usb_power = true, .charging = true};
    fake_hal_set_milliseconds(0);
    charge_screen_draw_at(&canvas, &theme, battery, CHARGE_SCREEN_FRAME_MS);
    uint32_t first = frame_hash(&theme);
    fake_hal_set_milliseconds(9999);
    charge_screen_draw_at(&canvas, &theme, battery, CHARGE_SCREEN_FRAME_MS);
    CHECK_EQUAL(first, frame_hash(&theme));
}

int main(void)
{
    fake_hal_reset();
    i18n_detach();
    static const TestCase cases[] = {
        TEST_CASE(battery_percentage_dominates_a_full_width_slim_gauge),
        TEST_CASE(only_visible_battery_changes_require_a_usb_redraw),
        TEST_CASE(charge_and_usb_details_fit_every_size_and_orientation),
        TEST_CASE(usb_voltage_is_hidden_and_the_real_mode_and_charge_are_visible),
        TEST_CASE(an_unknown_battery_has_no_invented_percent_or_voltage),
        TEST_CASE(usb_power_does_not_mean_the_battery_is_full),
        TEST_CASE(an_unknown_battery_is_never_reported_full),
        TEST_CASE(battery_power_is_never_reported_charging),
        TEST_CASE(a_known_full_battery_on_usb_is_reported_charged),
        TEST_CASE(actual_usb_charging_has_brief_clock_driven_energy_motion),
        TEST_CASE(charged_battery_and_noncharging_power_states_stay_still),
        TEST_CASE(storage_and_dac_screens_do_not_animate_with_the_charger),
        TEST_CASE(graphical_charge_layout_fits_all_six_variants_without_text_overlap),
        TEST_CASE(unknown_artwork_never_uses_a_stale_percentage),
        TEST_CASE(values_above_full_render_as_full_instead_of_overflowing_the_battery),
        TEST_CASE(charging_artwork_draws_identically_in_display_strips),
        TEST_CASE(explicit_frame_time_keeps_the_picture_independent_of_clock_reads),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
