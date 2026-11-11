#include <string.h>

#include "check.h"
#include "fonts/fonts.h"
#include "gfx/color.h"
#include "gfx/text.h"
#include "ui/header.h"
#include "ui/icons.h"
#include "ui/status_bar.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    STATUS_HEADER_HEIGHT = 12,
    STATUS_ICON_SIZE = 6,
    LOW_BATTERY = 10,
    HALF_BATTERY = 50,
    PANEL_WIDTH = 120,
    PANEL_HEIGHT = 30,
    PANEL_PADDING = 12,
    PANEL_ICON_SIZE = 14
};

static Theme status_theme(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.header_height = STATUS_HEADER_HEIGHT;
    theme.metrics.status_icon_size = STATUS_ICON_SIZE;
    return theme;
}

static Canvas draw_status(const Theme *theme, StatusBarState state)
{
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, theme, "");
    status_bar_draw(&canvas, theme, state);
    return canvas;
}

static StatusBarState battery_at(uint8_t percent, bool usb_power)
{
    return (StatusBarState){.playback = STATUS_PLAYBACK_NONE,
                            .battery_known = true,
                            .battery_percent = percent,
                            .usb_power = usb_power};
}

static void an_idle_player_without_a_battery_reading_draws_nothing(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_NONE});
    Rect header = {0, 0, TEST_SCREEN_WIDTH, STATUS_HEADER_HEIGHT - 1};
    CHECK(ui_test_area_has_only(&canvas, header, theme.colors.header_background));
}

static void a_playing_player_shows_the_play_icon_at_the_left(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_PLAYING});
    Rect icon = status_bar_playback_area(&theme);
    Rect middle = {icon.x + 2, icon.y, 3, icon.height};
    CHECK(icon.x + icon.width <= TEST_HEADER_INSET);
    CHECK(ui_test_area_has_color(&canvas, middle, theme.colors.header_text));
}

static void a_paused_player_shows_two_bars(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_PAUSED});
    Rect icon = status_bar_playback_area(&theme);
    Rect first = {icon.x, icon.y, 1, icon.height};
    Rect gap = {icon.x + 2, icon.y, 2, icon.height};
    Rect last = {icon.x + icon.width - 1, icon.y, 1, icon.height};
    CHECK(ui_test_area_has_only(&canvas, first, theme.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, gap, theme.colors.header_text));
    CHECK(ui_test_area_has_only(&canvas, last, theme.colors.header_text));
}

static Theme panel_theme(int icon_size)
{
    Theme theme = TEST_THEME;
    theme.fonts.small = &FONT_INTER_REGULAR_16;
    theme.metrics.screen_width = PANEL_WIDTH;
    theme.metrics.screen_height = PANEL_HEIGHT;
    theme.metrics.header_height = PANEL_HEIGHT;
    theme.metrics.padding = PANEL_PADDING;
    theme.metrics.status_icon_size = icon_size;
    return theme;
}

static Canvas draw_panel(const Theme *theme, StatusBarState state)
{
    static Color pixels[PANEL_WIDTH * PANEL_HEIGHT];
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, PANEL_WIDTH, PANEL_HEIGHT});
    header_draw(&canvas, theme, "");
    status_bar_draw(&canvas, theme, state);
    return canvas;
}

static Rect inner_of(Rect body)
{
    return (Rect){body.x + 2, body.y + 2, body.width - 4, body.height - 4};
}

static Rect tail_of(const Theme *theme, bool usb_power)
{
    Rect body = status_bar_battery_body(theme, usb_power);
    Rect area = status_bar_battery_area(theme, usb_power);
    return (Rect){body.x + body.width, area.y, area.x + area.width - body.x - body.width,
                  area.height};
}

static void the_battery_ends_at_the_right_padding_on_and_off_usb_power(void)
{
    Theme theme = panel_theme(PANEL_ICON_SIZE);
    Rect resting = status_bar_battery_area(&theme, false);
    Rect powered = status_bar_battery_area(&theme, true);
    CHECK_EQUAL(PANEL_WIDTH - PANEL_PADDING, resting.x + resting.width);
    CHECK_EQUAL(PANEL_WIDTH - PANEL_PADDING, powered.x + powered.width);
    CHECK(resting.y >= 0 && resting.y + resting.height <= PANEL_HEIGHT);
}

static void the_battery_body_is_two_icons_wide_and_frames_the_icon_height(void)
{
    Theme theme = panel_theme(PANEL_ICON_SIZE);
    Rect body = status_bar_battery_body(&theme, false);
    CHECK_EQUAL(PANEL_ICON_SIZE + 2, body.height);
    CHECK_EQUAL(2 * body.height, body.width);
}

static void the_fill_follows_the_charge(void)
{
    CHECK_EQUAL(0, status_bar_battery_fill_width(0, 16));
    CHECK_EQUAL(8, status_bar_battery_fill_width(HALF_BATTERY, 16));
    CHECK_EQUAL(15, status_bar_battery_fill_width(99, 16));
    CHECK_EQUAL(16, status_bar_battery_fill_width(100, 16));
    CHECK_EQUAL(16, status_bar_battery_fill_width(250, 16));
}

static void a_fill_too_thin_to_read_as_a_bar_is_left_out(void)
{
    CHECK_EQUAL(0, status_bar_battery_fill_width(1, 28));
    CHECK_EQUAL(0, status_bar_battery_fill_width(10, 28));
    CHECK_EQUAL(3, status_bar_battery_fill_width(11, 28));
}

static void the_percentage_is_the_number_alone_capped_at_one_hundred(void)
{
    char text[STATUS_BAR_PERCENT_CAPACITY];
    status_bar_percent_text(73, text, sizeof text);
    CHECK(strcmp("73", text) == 0);
    status_bar_percent_text(100, text, sizeof text);
    CHECK(strcmp("100", text) == 0);
    status_bar_percent_text(0, text, sizeof text);
    CHECK(strcmp("0", text) == 0);
    status_bar_percent_text(250, text, sizeof text);
    CHECK(strcmp("100", text) == 0);
}

static void a_panel_sized_battery_fits_three_digits_inside_its_body(void)
{
    Theme theme = panel_theme(PANEL_ICON_SIZE);
    char widest[STATUS_BAR_PERCENT_CAPACITY];
    status_bar_percent_text(100, widest, sizeof widest);
    CHECK(status_bar_battery_digits_fit(&theme));
    CHECK(text_width(theme.fonts.small, widest) <=
          inner_of(status_bar_battery_body(&theme, false)).width);
}

static void the_number_takes_contrasting_ink_on_each_side_of_the_fill(void)
{
    Theme theme = panel_theme(PANEL_ICON_SIZE);
    Canvas canvas = draw_panel(&theme, battery_at(HALF_BATTERY, false));
    Rect inner = inner_of(status_bar_battery_body(&theme, false));
    int filled = status_bar_battery_fill_width(HALF_BATTERY, inner.width);
    Rect full = {inner.x, inner.y, filled, inner.height};
    Rect empty = {inner.x + filled, inner.y, inner.width - filled, inner.height};
    CHECK(ui_test_area_has_color(&canvas, full, theme.colors.header_background));
    CHECK(ui_test_area_has_color(&canvas, empty, theme.colors.header_text));
}

static void a_battery_too_small_for_digits_shows_the_fill_alone(void)
{
    Theme theme = panel_theme(STATUS_ICON_SIZE);
    CHECK(!status_bar_battery_digits_fit(&theme));
    Canvas canvas = draw_panel(&theme, battery_at(HALF_BATTERY, false));
    Rect inner = inner_of(status_bar_battery_body(&theme, false));
    int filled = status_bar_battery_fill_width(HALF_BATTERY, inner.width);
    Rect full = {inner.x, inner.y, filled, inner.height};
    Rect empty = {inner.x + filled, inner.y, inner.width - filled, inner.height};
    CHECK(ui_test_area_has_only(&canvas, full, theme.colors.header_text));
    CHECK(ui_test_area_has_only(&canvas, empty, theme.colors.header_background));
}

static Color tinted(const Theme *theme, Color color)
{
    return color_between(color, theme->colors.header_text, 1, 3);
}

static void a_mid_charge_battery_is_drawn_in_the_header_text_color(void)
{
    Theme theme = TEST_THEME;
    StatusBatteryLook look = status_bar_battery_look(&theme, battery_at(HALF_BATTERY, false));
    CHECK_EQUAL(theme.colors.header_text, look.outline);
    CHECK_EQUAL(theme.colors.header_text, look.fill);
    CHECK_EQUAL(theme.colors.header_background, look.on_fill);
    CHECK_EQUAL(theme.colors.header_text, look.on_empty);
}

static void a_low_battery_fills_and_numbers_in_the_danger_color(void)
{
    Theme theme = TEST_THEME;
    StatusBatteryLook look =
        status_bar_battery_look(&theme, battery_at(STATUS_BAR_LOW_BATTERY_PERCENT, false));
    CHECK_EQUAL(theme.colors.header_text, look.outline);
    CHECK_EQUAL(tinted(&theme, theme.colors.danger), look.fill);
    CHECK_EQUAL(tinted(&theme, theme.colors.danger), look.on_empty);
    look = status_bar_battery_look(&theme, battery_at(STATUS_BAR_LOW_BATTERY_PERCENT + 1, false));
    CHECK_EQUAL(theme.colors.header_text, look.fill);
}

static void a_critical_battery_turns_its_outline_danger_too(void)
{
    Theme theme = TEST_THEME;
    StatusBatteryLook look =
        status_bar_battery_look(&theme, battery_at(STATUS_BAR_CRITICAL_BATTERY_PERCENT, false));
    CHECK_EQUAL(tinted(&theme, theme.colors.danger), look.outline);
    look =
        status_bar_battery_look(&theme, battery_at(STATUS_BAR_CRITICAL_BATTERY_PERCENT + 1, false));
    CHECK_EQUAL(theme.colors.header_text, look.outline);
}

static void a_battery_on_usb_power_fills_in_the_success_color_even_when_low(void)
{
    Theme theme = TEST_THEME;
    StatusBatteryLook look = status_bar_battery_look(&theme, battery_at(LOW_BATTERY / 2, true));
    CHECK_EQUAL(tinted(&theme, theme.colors.success), look.fill);
    CHECK_EQUAL(theme.colors.header_text, look.outline);
    CHECK_EQUAL(theme.colors.header_text, look.on_empty);
}

static void a_charging_battery_has_a_success_bolt_and_a_charged_one_a_neutral_bolt(void)
{
    Theme theme = TEST_THEME;
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.charging = true;
    CHECK_EQUAL(tinted(&theme, theme.colors.success), status_bar_battery_look(&theme, state).bolt);
    state.charging = false;
    CHECK_EQUAL(theme.colors.header_text, status_bar_battery_look(&theme, state).bolt);
}

static void state_colors_lean_toward_the_header_text_but_keep_their_hue(void)
{
    Theme theme = TEST_THEME;
    Color danger = tinted(&theme, theme.colors.danger);
    CHECK(danger != theme.colors.danger);
    CHECK(danger != theme.colors.header_text);
}

static void the_number_on_every_fill_is_knocked_out_in_the_header_background(void)
{
    Theme theme = TEST_THEME;
    Color background = theme.colors.header_background;
    CHECK_EQUAL(background,
                status_bar_battery_look(&theme, battery_at(HALF_BATTERY, true)).on_fill);
    CHECK_EQUAL(background,
                status_bar_battery_look(&theme, battery_at(LOW_BATTERY, false)).on_fill);
    CHECK_EQUAL(background,
                status_bar_battery_look(&theme, battery_at(HALF_BATTERY, false)).on_fill);
}

static void a_battery_on_usb_power_ends_in_a_bolt_instead_of_the_nub(void)
{
    Theme theme = panel_theme(PANEL_ICON_SIZE);
    Color bolt = tinted(&theme, theme.colors.success);
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.charging = true;
    Canvas powered = draw_panel(&theme, state);
    Rect powered_tail = tail_of(&theme, true);
    CHECK(powered_tail.width == icon_width(ICON_BOLT, PANEL_ICON_SIZE));
    CHECK(ui_test_area_has_color(&powered, powered_tail, bolt));
    Canvas resting = draw_panel(&theme, battery_at(HALF_BATTERY, false));
    Rect resting_tail = tail_of(&theme, false);
    CHECK(resting_tail.width < powered_tail.width);
    CHECK(!ui_test_area_has_color(&resting, resting_tail, bolt));
    CHECK(ui_test_area_has_color(&resting, resting_tail, theme.colors.header_text));
}

static void an_unknown_battery_draws_no_battery(void)
{
    Theme theme = panel_theme(PANEL_ICON_SIZE);
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.battery_known = false;
    Canvas canvas = draw_panel(&theme, state);
    CHECK(ui_test_area_has_only(&canvas, status_bar_battery_area(&theme, true),
                                theme.colors.header_background));
}

static void a_locked_player_shows_a_padlock(void)
{
    Theme theme = status_theme();
    Rect lock = status_bar_lock_area(&theme);
    Canvas unlocked = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_NONE});
    CHECK(!ui_test_area_has_color(&unlocked, lock, theme.colors.header_text));
    Canvas locked =
        draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_NONE, .locked = true});
    Rect body = {lock.x, lock.y + lock.height - 2, lock.width, 1};
    CHECK(ui_test_area_has_only(&locked, body, theme.colors.header_text));
    CHECK(lock.x > status_bar_bluetooth_area(&theme).x);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_locked_player_shows_a_padlock),
        TEST_CASE(an_idle_player_without_a_battery_reading_draws_nothing),
        TEST_CASE(a_playing_player_shows_the_play_icon_at_the_left),
        TEST_CASE(a_paused_player_shows_two_bars),
        TEST_CASE(the_battery_ends_at_the_right_padding_on_and_off_usb_power),
        TEST_CASE(the_battery_body_is_two_icons_wide_and_frames_the_icon_height),
        TEST_CASE(the_fill_follows_the_charge),
        TEST_CASE(a_fill_too_thin_to_read_as_a_bar_is_left_out),
        TEST_CASE(the_percentage_is_the_number_alone_capped_at_one_hundred),
        TEST_CASE(a_panel_sized_battery_fits_three_digits_inside_its_body),
        TEST_CASE(the_number_takes_contrasting_ink_on_each_side_of_the_fill),
        TEST_CASE(a_battery_too_small_for_digits_shows_the_fill_alone),
        TEST_CASE(a_mid_charge_battery_is_drawn_in_the_header_text_color),
        TEST_CASE(a_low_battery_fills_and_numbers_in_the_danger_color),
        TEST_CASE(a_critical_battery_turns_its_outline_danger_too),
        TEST_CASE(a_battery_on_usb_power_fills_in_the_success_color_even_when_low),
        TEST_CASE(a_charging_battery_has_a_success_bolt_and_a_charged_one_a_neutral_bolt),
        TEST_CASE(state_colors_lean_toward_the_header_text_but_keep_their_hue),
        TEST_CASE(the_number_on_every_fill_is_knocked_out_in_the_header_background),
        TEST_CASE(a_battery_on_usb_power_ends_in_a_bolt_instead_of_the_nub),
        TEST_CASE(an_unknown_battery_draws_no_battery),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
