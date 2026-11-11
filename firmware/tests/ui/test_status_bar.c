#include "check.h"
#include "fonts/fonts.h"
#include "gfx/color.h"
#include "gfx/text.h"
#include "ui/header.h"
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

static Theme panel_theme(void)
{
    Theme theme = TEST_THEME;
    theme.fonts.small = &FONT_INTER_REGULAR_16;
    theme.metrics.screen_width = PANEL_WIDTH;
    theme.metrics.screen_height = PANEL_HEIGHT;
    theme.metrics.header_height = PANEL_HEIGHT;
    theme.metrics.padding = PANEL_PADDING;
    theme.metrics.status_icon_size = PANEL_ICON_SIZE;
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

static Rect filled_part(Rect body, uint8_t percent)
{
    Rect inner = inner_of(body);
    inner.width = status_bar_battery_fill_width(percent, inner.width);
    return inner;
}

static Rect empty_part(Rect body, uint8_t percent)
{
    Rect inner = inner_of(body);
    int filled = status_bar_battery_fill_width(percent, inner.width);
    return (Rect){inner.x + filled, inner.y, inner.width - filled, inner.height};
}

static Rect tip_of(const Theme *theme)
{
    Rect body = status_bar_battery_body(theme);
    Rect area = status_bar_battery_area(theme);
    return (Rect){body.x + body.width, area.y, area.x + area.width - body.x - body.width,
                  area.height};
}

static Color tinted(const Theme *theme, Color color)
{
    return color_between(color, theme->colors.header_text, 1, 3);
}

static void the_battery_ends_at_the_right_padding_in_about_two_dozen_pixels(void)
{
    Theme theme = panel_theme();
    Rect area = status_bar_battery_area(&theme);
    CHECK_EQUAL(PANEL_WIDTH - PANEL_PADDING, area.x + area.width);
    CHECK(area.width <= 24);
    CHECK(area.y >= 0 && area.y + area.height <= PANEL_HEIGHT);
}

static void the_battery_body_is_a_little_wider_than_the_icon_size_and_two_pixels_shorter(void)
{
    Theme theme = panel_theme();
    Rect body = status_bar_battery_body(&theme);
    CHECK_EQUAL(PANEL_ICON_SIZE - 2, body.height);
    CHECK_EQUAL(body.height * 8 / 5, body.width);
}

static void the_fill_follows_the_charge(void)
{
    CHECK_EQUAL(0, status_bar_battery_fill_width(0, 15));
    CHECK_EQUAL(7, status_bar_battery_fill_width(HALF_BATTERY, 15));
    CHECK_EQUAL(14, status_bar_battery_fill_width(99, 15));
    CHECK_EQUAL(15, status_bar_battery_fill_width(100, 15));
    CHECK_EQUAL(15, status_bar_battery_fill_width(250, 15));
}

static void a_fill_too_thin_to_read_as_a_bar_grows_to_the_narrowest_readable_bar(void)
{
    CHECK_EQUAL(3, status_bar_battery_fill_width(1, 15));
    CHECK_EQUAL(3, status_bar_battery_fill_width(LOW_BATTERY, 15));
    CHECK_EQUAL(3, status_bar_battery_fill_width(20, 15));
    CHECK_EQUAL(4, status_bar_battery_fill_width(30, 15));
    CHECK_EQUAL(2, status_bar_battery_fill_width(1, 2));
}

static void a_mid_charge_battery_is_drawn_in_the_header_text_color(void)
{
    Theme theme = TEST_THEME;
    StatusBatteryLook look = status_bar_battery_look(&theme, battery_at(HALF_BATTERY, false));
    CHECK_EQUAL(theme.colors.header_text, look.outline);
    CHECK_EQUAL(theme.colors.header_text, look.fill);
}

static void a_low_battery_fills_in_the_danger_color(void)
{
    Theme theme = TEST_THEME;
    StatusBatteryLook look =
        status_bar_battery_look(&theme, battery_at(STATUS_BAR_LOW_BATTERY_PERCENT, false));
    CHECK_EQUAL(theme.colors.header_text, look.outline);
    CHECK_EQUAL(tinted(&theme, theme.colors.danger), look.fill);
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
}

static void state_colors_lean_toward_the_header_text_but_keep_their_hue(void)
{
    Theme theme = TEST_THEME;
    Color danger = tinted(&theme, theme.colors.danger);
    CHECK(danger != theme.colors.danger);
    CHECK(danger != theme.colors.header_text);
}

static void a_charging_bolt_is_light_with_a_dark_halo_and_a_charged_one_plain_dark(void)
{
    Theme theme = TEST_THEME;
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.charging = true;
    StatusBatteryLook look = status_bar_battery_look(&theme, state);
    CHECK_EQUAL(theme.colors.header_text, look.bolt);
    CHECK(look.bolt_halo);
    state.charging = false;
    look = status_bar_battery_look(&theme, state);
    CHECK_EQUAL(theme.colors.header_background, look.bolt);
    CHECK(!look.bolt_halo);
}

static void a_charging_bolt_reads_over_both_the_fill_and_the_empty_part(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.charging = true;
    Canvas canvas = draw_panel(&theme, state);
    Rect body = status_bar_battery_body(&theme);
    CHECK(ui_test_area_has_color(&canvas, filled_part(body, HALF_BATTERY),
                                 theme.colors.header_background));
    CHECK(
        ui_test_area_has_color(&canvas, empty_part(body, HALF_BATTERY), theme.colors.header_text));
    Canvas resting = draw_panel(&theme, battery_at(HALF_BATTERY, false));
    CHECK(ui_test_area_has_only(&resting, empty_part(body, HALF_BATTERY),
                                theme.colors.header_background));
}

static void a_charged_battery_shows_a_dark_bolt_on_its_full_fill(void)
{
    Theme theme = panel_theme();
    Canvas canvas = draw_panel(&theme, battery_at(100, true));
    Rect inner = inner_of(status_bar_battery_body(&theme));
    CHECK(ui_test_area_has_color(&canvas, inner, theme.colors.header_background));
    CHECK(ui_test_area_has_color(&canvas, inner, tinted(&theme, theme.colors.success)));
    CHECK(!ui_test_area_has_color(&canvas, inner, theme.colors.header_text));
}

static void the_outline_stays_unbroken_around_the_bolt_while_charging_or_charged(void)
{
    Theme theme = panel_theme();
    Rect body = status_bar_battery_body(&theme);
    Rect edges[] = {{body.x, body.y, body.width, 1},
                    {body.x, body.y + body.height - 1, body.width, 1},
                    {body.x, body.y, 1, body.height},
                    {body.x + body.width - 1, body.y, 1, body.height}};
    for (int charging = 0; charging < 2; charging++) {
        StatusBarState state = battery_at(charging ? HALF_BATTERY : 100, true);
        state.charging = charging != 0;
        Canvas canvas = draw_panel(&theme, state);
        for (int edge = 0; edge < 4; edge++) {
            CHECK(ui_test_area_has_only(&canvas, edges[edge], theme.colors.header_text));
        }
    }
}

static void the_tip_stays_a_plain_nub_on_usb_power(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.charging = true;
    Canvas canvas = draw_panel(&theme, state);
    Rect tip = tip_of(&theme);
    Rect middle = {tip.x, tip.y + tip.height / 2, tip.width, 1};
    CHECK(ui_test_area_has_only(&canvas, middle, theme.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, tip, tinted(&theme, theme.colors.success)));
}

static Rect percent_area(const Theme *theme, const char *text)
{
    Rect body = status_bar_battery_body(theme);
    int width = text_width(theme->fonts.small, text);
    return (Rect){body.x - PANEL_PADDING / 2 - width, body.y, width, body.height};
}

static void the_percentage_is_hidden_unless_the_app_passes_it(void)
{
    Theme theme = panel_theme();
    Canvas canvas = draw_panel(&theme, battery_at(HALF_BATTERY, false));
    CHECK(ui_test_area_has_only(&canvas, percent_area(&theme, "50%"),
                                theme.colors.header_background));
}

static void a_shown_percentage_sits_left_of_the_battery_level_with_its_body(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    state.percent = "50%";
    Canvas canvas = draw_panel(&theme, state);
    Rect text = percent_area(&theme, "50%");
    Rect body = status_bar_battery_body(&theme);
    CHECK(ui_test_area_has_color(&canvas, text, theme.colors.header_text));
    Rect gap = {text.x + text.width, body.y, body.x - text.x - text.width, body.height};
    CHECK(ui_test_area_has_only(&canvas, gap, theme.colors.header_background));
    Rect above = {text.x, body.y - 2, text.width, 2};
    CHECK(ui_test_area_has_only(&canvas, above, theme.colors.header_background));
}

static void a_shown_percentage_turns_red_when_low_and_stays_plain_on_usb_power(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(LOW_BATTERY, false);
    state.percent = "10%";
    Canvas low = draw_panel(&theme, state);
    Rect text = percent_area(&theme, "10%");
    CHECK(ui_test_area_has_color(&low, text, tinted(&theme, theme.colors.danger)));
    state.usb_power = true;
    Canvas powered = draw_panel(&theme, state);
    CHECK(ui_test_area_has_color(&powered, text, theme.colors.header_text));
    CHECK(!ui_test_area_has_color(&powered, text, tinted(&theme, theme.colors.danger)));
}

static void an_unknown_battery_draws_no_battery(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.battery_known = false;
    Canvas canvas = draw_panel(&theme, state);
    CHECK(ui_test_area_has_only(&canvas, status_bar_battery_area(&theme),
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
        TEST_CASE(the_battery_ends_at_the_right_padding_in_about_two_dozen_pixels),
        TEST_CASE(the_battery_body_is_a_little_wider_than_the_icon_size_and_two_pixels_shorter),
        TEST_CASE(the_fill_follows_the_charge),
        TEST_CASE(a_fill_too_thin_to_read_as_a_bar_grows_to_the_narrowest_readable_bar),
        TEST_CASE(a_mid_charge_battery_is_drawn_in_the_header_text_color),
        TEST_CASE(a_low_battery_fills_in_the_danger_color),
        TEST_CASE(a_critical_battery_turns_its_outline_danger_too),
        TEST_CASE(a_battery_on_usb_power_fills_in_the_success_color_even_when_low),
        TEST_CASE(state_colors_lean_toward_the_header_text_but_keep_their_hue),
        TEST_CASE(a_charging_bolt_is_light_with_a_dark_halo_and_a_charged_one_plain_dark),
        TEST_CASE(a_charging_bolt_reads_over_both_the_fill_and_the_empty_part),
        TEST_CASE(a_charged_battery_shows_a_dark_bolt_on_its_full_fill),
        TEST_CASE(the_outline_stays_unbroken_around_the_bolt_while_charging_or_charged),
        TEST_CASE(the_tip_stays_a_plain_nub_on_usb_power),
        TEST_CASE(the_percentage_is_hidden_unless_the_app_passes_it),
        TEST_CASE(a_shown_percentage_sits_left_of_the_battery_level_with_its_body),
        TEST_CASE(a_shown_percentage_turns_red_when_low_and_stays_plain_on_usb_power),
        TEST_CASE(an_unknown_battery_draws_no_battery),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
