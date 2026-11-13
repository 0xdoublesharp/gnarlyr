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
    PANEL_ICON_SIZE = 14,
    PORTRAIT_STATUS_HEIGHT = 20
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

static int border_of(Rect body)
{
    return body.height / 8;
}

static Rect inner_of(Rect body)
{
    int inset = border_of(body) + 1;
    return (Rect){body.x + inset, body.y + inset, body.width - 2 * inset, body.height - 2 * inset};
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

static void the_battery_ends_at_the_right_padding_and_fits_the_narrowest_status_band(void)
{
    Theme theme = panel_theme();
    Rect area = status_bar_battery_area(&theme);
    CHECK_EQUAL(PANEL_WIDTH - PANEL_PADDING, area.x + area.width);
    CHECK(area.width <= 30);
    CHECK(area.y >= 0 && area.y + area.height <= PANEL_HEIGHT);
    theme.metrics.status_height = PORTRAIT_STATUS_HEIGHT;
    area = status_bar_battery_area(&theme);
    CHECK(area.y >= 1 && area.y + area.height <= PORTRAIT_STATUS_HEIGHT - 1);
}

static void the_battery_centres_on_line_one_and_the_play_icon_on_its_own_line(void)
{
    Theme theme = panel_theme();
    for (int band = 0; band < 2; band++) {
        theme.metrics.status_height = band == 0 ? 0 : PORTRAIT_STATUS_HEIGHT;
        Rect body = status_bar_battery_body(&theme);
        Rect play = status_bar_playback_area(&theme);
        int status = theme.metrics.status_height;
        int header = theme.metrics.header_height;
        if (status > 0 && header > status) {
            int battery = 2 * body.y + body.height - status;
            int icon = 2 * play.y + play.height - (status + header - 1);
            CHECK(battery >= -1 && battery <= 1);
            CHECK(icon >= -1 && icon <= 1);
        } else {
            CHECK_EQUAL(2 * play.y + play.height, 2 * body.y + body.height);
        }
    }
}

static void the_battery_body_frames_the_icon_size_and_is_thirteen_eighths_as_wide(void)
{
    Theme theme = panel_theme();
    Rect body = status_bar_battery_body(&theme);
    CHECK_EQUAL(PANEL_ICON_SIZE + 2, body.height);
    CHECK_EQUAL(body.height * 13 / 8, body.width);
}

static void a_full_battery_keeps_a_two_pixel_outline_and_a_one_pixel_gap_before_the_fill(void)
{
    Theme theme = panel_theme();
    Canvas canvas = draw_panel(&theme, battery_at(100, false));
    Rect body = status_bar_battery_body(&theme);
    CHECK_EQUAL(2, border_of(body));
    Rect outline = {body.x, body.y + body.height / 2, 2, 1};
    Rect gap = {body.x + 2, body.y + 2, 1, body.height - 4};
    CHECK(ui_test_area_has_only(&canvas, outline, theme.colors.header_text));
    CHECK(ui_test_area_has_only(&canvas, gap, theme.colors.header_background));
    CHECK(ui_test_area_has_only(&canvas, inner_of(body), theme.colors.header_text));
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

static void usb_power_without_charging_shows_a_light_plug_instead_of_the_bolt(void)
{
    Theme theme = TEST_THEME;
    StatusBarState state = battery_at(HALF_BATTERY, true);
    state.charging_off = true;
    StatusBatteryLook look = status_bar_battery_look(&theme, state);
    CHECK_EQUAL(ICON_PLUG, look.sign);
    CHECK_EQUAL(theme.colors.header_text, look.bolt);
    CHECK(look.bolt_halo);
    state.charging_off = false;
    state.charging = true;
    CHECK_EQUAL(ICON_BOLT, status_bar_battery_look(&theme, state).sign);
}

static void the_plug_and_the_bolt_draw_differently(void)
{
    Theme theme = panel_theme();
    StatusBarState plug = battery_at(HALF_BATTERY, true);
    plug.charging_off = true;
    StatusBarState bolt = battery_at(HALF_BATTERY, true);
    bolt.charging = true;
    Rect inner = inner_of(status_bar_battery_body(&theme));
    Canvas first = draw_panel(&theme, plug);
    int differences = 0;
    Color plugged[PANEL_WIDTH];
    for (int x = 0; x < inner.width && x < PANEL_WIDTH; x++) {
        plugged[x] = canvas_pixel_at(&first, inner.x + x, inner.y + inner.height / 4);
    }
    Canvas second = draw_panel(&theme, bolt);
    for (int x = 0; x < inner.width && x < PANEL_WIDTH; x++) {
        differences +=
            canvas_pixel_at(&second, inner.x + x, inner.y + inner.height / 4) != plugged[x];
    }
    CHECK(differences > 0);
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
    for (int mode = 0; mode < 4; mode++) {
        bool charging = (mode & 1) != 0;
        StatusBarState state = battery_at(charging ? HALF_BATTERY : 100, true);
        state.charging = charging;
        state.segmented = mode >= 2;
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

static void the_clock_is_hidden_unless_the_app_passes_it(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    Canvas canvas = draw_panel(&theme, state);
    state.clock = "09:41";
    CHECK(ui_test_area_has_only(&canvas, status_bar_clock_area(&theme, state),
                                theme.colors.header_background));
}

static void a_shown_clock_sits_left_of_the_battery_in_the_header_text(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    state.clock = "09:41";
    Canvas canvas = draw_panel(&theme, state);
    Rect clock = status_bar_clock_area(&theme, state);
    Rect body = status_bar_battery_body(&theme);
    CHECK(status_bar_clock_fits(&theme, state));
    CHECK(clock.x + clock.width + PANEL_PADDING / 2 == body.x);
    CHECK(ui_test_area_has_color(&canvas, clock, theme.colors.header_text));
    Rect gap = {clock.x + clock.width, body.y, body.x - clock.x - clock.width, body.height};
    CHECK(ui_test_area_has_only(&canvas, gap, theme.colors.header_background));
}

static void a_shown_percentage_pushes_the_clock_further_left(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    state.clock = "9:41";
    Rect alone = status_bar_clock_area(&theme, state);
    state.percent = "50%";
    Rect pushed = status_bar_clock_area(&theme, state);
    CHECK_EQUAL(alone.x - PANEL_PADDING / 2 - text_width(theme.fonts.small, "50%"), pushed.x);
    CHECK(pushed.x + pushed.width <= percent_area(&theme, "50%").x);
}

static void a_clock_that_would_reach_the_left_icons_is_left_out(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    state.clock = "09:41";
    state.playback = STATUS_PLAYBACK_PLAYING;
    Rect clock = status_bar_clock_area(&theme, state);
    CHECK(clock.x > status_bar_left_end(&theme, state));
    CHECK(!status_bar_clock_fits(&theme, state));
    Canvas canvas = draw_panel(&theme, state);
    Rect right = {status_bar_left_end(&theme, state) + 1, clock.y,
                  status_bar_battery_body(&theme).x - status_bar_left_end(&theme, state) - 2,
                  clock.height};
    CHECK(ui_test_area_has_only(&canvas, right, theme.colors.header_background));
}

static void without_a_battery_reading_the_clock_ends_at_the_right_padding(void)
{
    Theme theme = panel_theme();
    StatusBarState state = {.clock = "09:41"};
    Rect clock = status_bar_clock_area(&theme, state);
    CHECK_EQUAL(theme.metrics.screen_width - PANEL_PADDING, clock.x + clock.width);
    CHECK(status_bar_clock_fits(&theme, state));
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

static void volume_draws_a_speaker_and_the_player_units_when_enabled(void)
{
    Theme theme = panel_theme();
    Canvas canvas = draw_panel(&theme, (StatusBarState){.volume = "47"});
    Rect volume = {12, 7, 40, 16};
    CHECK(ui_test_area_has_color(&canvas, volume, theme.colors.header_text));
}

static void a_queue_position_is_centred_when_the_title_is_hidden(void)
{
    Theme theme = panel_theme();
    Canvas canvas = draw_panel(&theme, (StatusBarState){.position = "3/12"});
    int width = text_width(theme.fonts.small, "3/12");
    Rect position = {(PANEL_WIDTH - width) / 2, 7, width, 16};
    CHECK(ui_test_area_has_color(&canvas, position, theme.colors.header_text));
}

static void a_segmented_battery_has_gaps_between_five_green_bars(void)
{
    Theme theme = panel_theme();
    StatusBarState state = battery_at(100, false);
    state.segmented = true;
    Canvas canvas = draw_panel(&theme, state);
    Rect inner = inner_of(status_bar_battery_body(&theme));
    CHECK(ui_test_area_has_color(&canvas, inner, tinted(&theme, theme.colors.success)));
    CHECK(ui_test_area_has_color(&canvas, inner, theme.colors.header_background));
}

static void segmented_levels_round_up_and_keep_the_low_and_critical_thresholds(void)
{
    Theme theme = panel_theme();
    Rect body = status_bar_battery_body(&theme);
    Rect inner = inner_of(body);
    for (int percent = 0; percent <= 255; percent++) {
        StatusBarState state = battery_at((uint8_t)percent, false);
        state.segmented = true;
        Canvas canvas = draw_panel(&theme, state);
        Color fill = tinted(&theme, percent <= 15 ? theme.colors.danger : theme.colors.success);
        Color outline =
            percent <= 5 ? tinted(&theme, theme.colors.danger) : theme.colors.header_text;
        CHECK_EQUAL(outline, canvas_pixel_at(&canvas, body.x, body.y));
        int bars = percent >= 100 ? 5 : (percent + 19) / 20;
        for (int bar = 0; bar < 5; bar++) {
            int x = inner.x + bar * (inner.width / 5);
            CHECK_EQUAL(bar < bars ? fill : theme.colors.header_background,
                        canvas_pixel_at(&canvas, x, inner.y));
            CHECK_EQUAL(theme.colors.header_background,
                        canvas_pixel_at(&canvas, x + inner.width / 5 - 1, inner.y));
        }
    }
}

static void repeat_one_adds_a_song_badge_and_repeat_off_draws_nothing(void)
{
    Theme theme = panel_theme();
    StatusBarState state = {.repeat = 2};
    Rect all = status_bar_item_area(&theme, &state, STATUS_BAR_REPEAT);
    Canvas canvas = draw_panel(&theme, state);
    CHECK(ui_test_area_has_color(&canvas, all, theme.colors.header_text));
    state.repeat = 1;
    Rect one = status_bar_item_area(&theme, &state, STATUS_BAR_REPEAT);
    CHECK(one.width > all.width);
    canvas = draw_panel(&theme, state);
    Rect badge = {all.x + all.width + 1, one.y, one.width - all.width - 1, one.height};
    CHECK(ui_test_area_has_color(&canvas, badge, theme.colors.header_text));
    state.repeat = 0;
    canvas = draw_panel(&theme, state);
    CHECK(ui_test_area_has_only(&canvas, one, theme.colors.header_background));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(segmented_levels_round_up_and_keep_the_low_and_critical_thresholds),
        TEST_CASE(repeat_one_adds_a_song_badge_and_repeat_off_draws_nothing),
        TEST_CASE(volume_draws_a_speaker_and_the_player_units_when_enabled),
        TEST_CASE(a_queue_position_is_centred_when_the_title_is_hidden),
        TEST_CASE(a_segmented_battery_has_gaps_between_five_green_bars),
        TEST_CASE(a_locked_player_shows_a_padlock),
        TEST_CASE(an_idle_player_without_a_battery_reading_draws_nothing),
        TEST_CASE(a_playing_player_shows_the_play_icon_at_the_left),
        TEST_CASE(a_paused_player_shows_two_bars),
        TEST_CASE(the_battery_ends_at_the_right_padding_and_fits_the_narrowest_status_band),
        TEST_CASE(the_battery_centres_on_line_one_and_the_play_icon_on_its_own_line),
        TEST_CASE(the_battery_body_frames_the_icon_size_and_is_thirteen_eighths_as_wide),
        TEST_CASE(a_full_battery_keeps_a_two_pixel_outline_and_a_one_pixel_gap_before_the_fill),
        TEST_CASE(the_fill_follows_the_charge),
        TEST_CASE(a_fill_too_thin_to_read_as_a_bar_grows_to_the_narrowest_readable_bar),
        TEST_CASE(a_mid_charge_battery_is_drawn_in_the_header_text_color),
        TEST_CASE(a_low_battery_fills_in_the_danger_color),
        TEST_CASE(a_critical_battery_turns_its_outline_danger_too),
        TEST_CASE(a_battery_on_usb_power_fills_in_the_success_color_even_when_low),
        TEST_CASE(state_colors_lean_toward_the_header_text_but_keep_their_hue),
        TEST_CASE(a_charging_bolt_is_light_with_a_dark_halo_and_a_charged_one_plain_dark),
        TEST_CASE(usb_power_without_charging_shows_a_light_plug_instead_of_the_bolt),
        TEST_CASE(the_plug_and_the_bolt_draw_differently),
        TEST_CASE(a_charging_bolt_reads_over_both_the_fill_and_the_empty_part),
        TEST_CASE(a_charged_battery_shows_a_dark_bolt_on_its_full_fill),
        TEST_CASE(the_outline_stays_unbroken_around_the_bolt_while_charging_or_charged),
        TEST_CASE(the_tip_stays_a_plain_nub_on_usb_power),
        TEST_CASE(the_percentage_is_hidden_unless_the_app_passes_it),
        TEST_CASE(a_shown_percentage_sits_left_of_the_battery_level_with_its_body),
        TEST_CASE(a_shown_percentage_turns_red_when_low_and_stays_plain_on_usb_power),
        TEST_CASE(the_clock_is_hidden_unless_the_app_passes_it),
        TEST_CASE(a_shown_clock_sits_left_of_the_battery_in_the_header_text),
        TEST_CASE(a_shown_percentage_pushes_the_clock_further_left),
        TEST_CASE(a_clock_that_would_reach_the_left_icons_is_left_out),
        TEST_CASE(without_a_battery_reading_the_clock_ends_at_the_right_padding),
        TEST_CASE(an_unknown_battery_draws_no_battery),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
