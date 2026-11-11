#include <string.h>

#include "app/extension.h"
#include "check.h"
#include "core/calendar.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "hal/clock.h"
#include "screens/set_clock_screen.h"
#include "text_spy.h"

static Theme theme;
static Settings settings;
static Toast toast;
static SetClockScreen editor;
static Color pixels[480 * 222];

static void start(uint32_t utc, int16_t offset)
{
    fake_hal_reset();
    fake_hal_set_wall(utc, offset);
    fake_hal_set_rtc_answer((HalRtcRead){.status = HAL_RTC_OK});
    theme = BOARD_THEME;
    settings = settings_defaults();
    toast = (Toast){0};
    set_clock_screen_init(&editor, (SetClockConfig){&theme, &settings, &toast});
    editor.screen.enter(&editor, 0);
}

static ScreenAction key(Key key, KeyGesture gesture)
{
    return editor.screen.handle_key(&editor, (KeyEvent){.key = key, .gesture = gesture}, 0);
}

static void move(int field)
{
    while (editor.field != field) {
        key(KEY_MENU, KEY_GESTURE_PRESS);
    }
}

static void change(int amount)
{
    for (int index = 0; index < (amount < 0 ? -amount : amount); index++) {
        key(amount > 0 ? KEY_VOLUME_UP : KEY_VOLUME_DOWN, KEY_GESTURE_PRESS);
    }
}

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->shown, text) == 0) {
            return true;
        }
    }
    return false;
}

static void draw(void)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    editor.screen.draw(&editor, &canvas);
}

static void fields_start_at_local_time_and_seconds_are_zeroed_on_save(void)
{
    start(1794390097u, -480);
    CHECK_EQUAL(2026, editor.date.year);
    CHECK_EQUAL(11, editor.date.month);
    CHECK_EQUAL(11, editor.date.day);
    CHECK_EQUAL(1, editor.hour);
    CHECK_EQUAL(41, editor.minute);
    move(SET_CLOCK_MINUTE);
    change(1);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_PLAY, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(1794390120u, fake_hal_rtc_written());
    CHECK_EQUAL(1794390120u, hal_clock_wall().utc_s);
    CHECK_EQUAL(-480, hal_clock_wall().offset_minutes);
}

static void changing_months_and_years_clamps_leap_days(void)
{
    start(0, 0);
    CHECK_EQUAL(2026, editor.date.year);
    change(-2);
    move(SET_CLOCK_DAY);
    change(30);
    CHECK_EQUAL(31, editor.date.day);
    move(SET_CLOCK_MONTH);
    change(1);
    CHECK_EQUAL(29, editor.date.day);
    move(SET_CLOCK_YEAR);
    change(1);
    CHECK_EQUAL(28, editor.date.day);
    change(-100);
    CHECK_EQUAL(2024, editor.date.year);
    change(100);
    CHECK_EQUAL(2099, editor.date.year);
}

static void field_navigation_wraps_and_holds_change_values(void)
{
    start(0, 0);
    key(KEY_MENU, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(SET_CLOCK_OFFSET, editor.field);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SET_CLOCK_YEAR, editor.field);
    move(SET_CLOCK_HOUR);
    key(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT);
    CHECK_EQUAL(23, editor.hour);
    change(1);
    CHECK_EQUAL(0, editor.hour);
    move(SET_CLOCK_MINUTE);
    change(-1);
    CHECK_EQUAL(59, editor.minute);
}

static void the_hour_field_uses_the_chosen_format_at_midnight_and_noon(void)
{
    start(0, 0);
    move(SET_CLOCK_HOUR);
    settings.clock_24h = false;
    draw();
    CHECK(saw("12:00 AM"));
    change(12);
    draw();
    CHECK(saw("12:00 PM"));
    settings.clock_24h = true;
    draw();
    CHECK(saw("12:00"));
}

static void cancel_and_failed_writes_leave_wall_time_unchanged(void)
{
    start(1794390060u, 60);
    change(1);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(0, fake_hal_rtc_read_count());
    CHECK_EQUAL(1794390060u, hal_clock_wall().utc_s);
    editor.screen.enter(&editor, 0);
    change(1);
    fake_hal_set_rtc_answer((HalRtcRead){.status = HAL_RTC_NO_ACK});
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_PLAY, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(1794390060u, hal_clock_wall().utc_s);
    CHECK(strcmp(toast.text, i18n_text(STR_SET_CLOCK_FAILED)) == 0);
}

static void every_field_and_the_controls_fit_all_six_layouts(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start(1794390060u, -480);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            default_theme_set_text_size(&theme, (TextSize)size);
            for (int field = 0; field < SET_CLOCK_FIELDS; field++) {
                move(field);
                draw();
                bool cancel_shown = false;
                for (int index = 0; index < text_spy_count(); index++) {
                    cancel_shown |= strstr(text_spy_entry(index)->shown, "cancel") != NULL;
                }
                CHECK(cancel_shown);
            }
            CHECK(saw(i18n_text(STR_SET_CLOCK_ZONE)));
            CHECK(saw("-08:00"));
        }
    }
}

static void a_missing_extension_cannot_set_an_unknown_clock(void)
{
    start(0, 0);
    fake_hal_break_module(EXTENSION_MODULE_NUMBER, true);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_PLAY, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(HAL_WALL_UNKNOWN, hal_clock_wall().source);
    CHECK_EQUAL(0, fake_hal_rtc_read_count());
    CHECK(strcmp(toast.text, i18n_text(STR_SET_CLOCK_FAILED)) == 0);
}

static void an_unknown_clock_becomes_known_and_uses_the_selected_offset(void)
{
    start(0, 0);
    CHECK_EQUAL(HAL_WALL_UNKNOWN, hal_clock_wall().source);
    move(SET_CLOCK_OFFSET);
    change(-32);
    CHECK_EQUAL(-480, editor.offset_minutes);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    uint32_t expected = calendar_days((CalendarDate){2026, 1, 1, 0}) * 86400u + 8 * 3600u;
    CHECK_EQUAL(expected, hal_clock_wall().utc_s);
    CHECK_EQUAL(HAL_WALL_PLAYER, hal_clock_wall().source);
    CHECK_EQUAL(-480, hal_clock_wall().offset_minutes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(fields_start_at_local_time_and_seconds_are_zeroed_on_save),
        TEST_CASE(changing_months_and_years_clamps_leap_days),
        TEST_CASE(field_navigation_wraps_and_holds_change_values),
        TEST_CASE(the_hour_field_uses_the_chosen_format_at_midnight_and_noon),
        TEST_CASE(cancel_and_failed_writes_leave_wall_time_unchanged),
        TEST_CASE(every_field_and_the_controls_fit_all_six_layouts),
        TEST_CASE(a_missing_extension_cannot_set_an_unknown_clock),
        TEST_CASE(an_unknown_clock_becomes_known_and_uses_the_selected_offset),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
