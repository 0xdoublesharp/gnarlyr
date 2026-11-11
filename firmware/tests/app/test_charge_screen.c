#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "screen_modules.h"
#include "screen_snapshot.h"
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

static void rect_fits(Rect area, const Theme *theme)
{
    CHECK(area.width > 0);
    CHECK(area.height > 0);
    CHECK(area.x >= 0);
    CHECK(area.y >= 0);
    CHECK(area.x + area.width <= theme->metrics.screen_width);
    CHECK(area.y + area.height <= theme->metrics.screen_height);
}

static bool battery_shows(const Theme *theme, Color color)
{
    Rect battery = charge_screen_art_layout(theme).battery;
    for (int y = battery.y; y < battery.y + battery.height; y++) {
        for (int x = battery.x; x < battery.x + battery.width; x++) {
            if (pixels[y * theme->metrics.screen_width + x] == color) {
                return true;
            }
        }
    }
    return false;
}

static void an_unknown_battery_has_no_invented_percent_or_voltage(void)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    text_spy_reset();
    charge_screen_draw(&canvas, &theme, (HalBattery){.usb_power = true});
    CHECK(saw(i18n_text(STR_POWER_UNKNOWN)));
    CHECK(saw(i18n_text(STR_POWER_USB)));
    CHECK(!battery_shows(&theme, theme.colors.success));
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
            rect_fits(layout.detail, &theme);
            CHECK(layout.battery.height >= 2 * font_line_height(theme.fonts.small));
            CHECK(layout.header.y + layout.header.height <= layout.battery.y);
            CHECK(layout.percent.y + layout.percent.height <= layout.status.y);
            CHECK(layout.status.y + layout.status.height <=
                  layout.detail.y - theme.metrics.padding);
            CHECK(layout.battery.y + layout.battery.height <=
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

static Color still[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];

static bool changes_stay_inside(const Theme *theme, Rect area)
{
    for (int y = 0; y < theme->metrics.screen_height; y++) {
        for (int x = 0; x < theme->metrics.screen_width; x++) {
            int index = y * theme->metrics.screen_width + x;
            if (pixels[index] != still[index] &&
                rect_is_empty(rect_intersection(area, (Rect){x, y, 1, 1}))) {
                return false;
            }
        }
    }
    return true;
}

static void charging_motion_redraws_only_the_inside_of_the_battery(void)
{
    static const HalBattery states[] = {
        {.known = true, .percent = 64, .charging = true, .usb_power = true},
        {.known = true, .percent = 2, .charging = true, .usb_power = true},
        {.charging = true, .usb_power = true},
    };
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = charge_theme(size, orientation);
            Canvas canvas = charge_canvas(&theme);
            Rect battery = charge_screen_art_layout(&theme).battery;
            for (size_t index = 0; index < sizeof states / sizeof states[0]; index++) {
                charge_screen_draw_at(&canvas, &theme, states[index], 0);
                memcpy(still, pixels, sizeof still);
                uint32_t peak = CHARGE_SCREEN_FRAME_MS * CHARGE_SCREEN_PHASES / 2;
                charge_screen_draw_at(&canvas, &theme, states[index], peak);
                CHECK(memcmp(still, pixels, sizeof still) != 0);
                CHECK(changes_stay_inside(&theme, battery));
            }
        }
    }
}

static void every_theme_paints_its_header_band_and_backdrop(void)
{
    HalBattery battery = {.known = true, .percent = 64, .charging = true, .usb_power = true};
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        Theme theme = BOARD_THEME;
        default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
        Canvas canvas = charge_canvas(&theme);
        charge_screen_draw_at(&canvas, &theme, battery, 0);
        CHECK_EQUAL(theme.colors.header_background, pixels[0]);
        CHECK_EQUAL(theme.colors.backdrop_top,
                    pixels[theme.metrics.header_height * theme.metrics.screen_width]);
        CHECK(battery_shows(&theme, theme.colors.success));
    }
}

static void a_nearly_empty_battery_still_shows_a_sliver_of_charge(void)
{
    Theme theme = BOARD_THEME;
    Canvas canvas = charge_canvas(&theme);
    charge_screen_draw_at(&canvas, &theme,
                          (HalBattery){.known = true, .percent = 1, .usb_power = true}, 0);
    CHECK(battery_shows(&theme, theme.colors.success));
    charge_screen_draw_at(&canvas, &theme, (HalBattery){.known = true, .usb_power = true}, 0);
    CHECK(!battery_shows(&theme, theme.colors.success));
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
        TEST_CASE(an_unknown_battery_has_no_invented_percent_or_voltage),
        TEST_CASE(usb_power_does_not_mean_the_battery_is_full),
        TEST_CASE(an_unknown_battery_is_never_reported_full),
        TEST_CASE(battery_power_is_never_reported_charging),
        TEST_CASE(a_known_full_battery_on_usb_is_reported_charged),
        TEST_CASE(actual_usb_charging_has_brief_clock_driven_energy_motion),
        TEST_CASE(charged_battery_and_noncharging_power_states_stay_still),
        TEST_CASE(graphical_charge_layout_fits_all_six_variants_without_text_overlap),
        TEST_CASE(unknown_artwork_never_uses_a_stale_percentage),
        TEST_CASE(values_above_full_render_as_full_instead_of_overflowing_the_battery),
        TEST_CASE(charging_artwork_draws_identically_in_display_strips),
        TEST_CASE(explicit_frame_time_keeps_the_picture_independent_of_clock_reads),
        TEST_CASE(charging_motion_redraws_only_the_inside_of_the_battery),
        TEST_CASE(every_theme_paints_its_header_band_and_backdrop),
        TEST_CASE(a_nearly_empty_battery_still_shows_a_sliver_of_charge),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
