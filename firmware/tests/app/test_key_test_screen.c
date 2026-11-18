#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/font.h"
#include "hal/display.h"
#include "screen_snapshot.h"
#include "screens/key_test_screen.h"
#include "ui/label.h"

enum {
    PLAY_READING = 242,
    UP_READING = 122,
    DOWN_READING = 1,
    IDLE_READING = 1023
};

static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static KeyTestScreen test;
static Theme theme;

static void open_test(void)
{
    fake_hal_reset();
    theme = BOARD_THEME;
    key_test_screen_init(&test, &theme);
    test.screen.enter(&test, 1000);
}

static void read_at(uint16_t reading, uint32_t now_ms)
{
    fake_hal_set_key_adc(reading);
    test.screen.update(&test, now_ms);
}

static void bands_follow_the_key_decoder(void)
{
    CHECK_EQUAL(KEY_VOLUME_DOWN, key_test_band(DOWN_READING));
    CHECK_EQUAL(KEY_VOLUME_UP, key_test_band(UP_READING));
    CHECK_EQUAL(KEY_PLAY, key_test_band(PLAY_READING));
    CHECK_EQUAL(KEY_MENU, key_test_band(350));
    CHECK_EQUAL(KEY_BACK, key_test_band(500));
    CHECK_EQUAL(KEY_COUNT, (int)key_test_band(190));
    CHECK_EQUAL(KEY_COUNT, (int)key_test_band(IDLE_READING));
}

static void each_band_change_is_kept_newest_first_with_its_time(void)
{
    open_test();
    read_at(PLAY_READING, 1100);
    read_at(PLAY_READING + 3, 1104);
    read_at(UP_READING, 1200);
    read_at(PLAY_READING, 1260);
    CHECK_EQUAL(4, test.count);
    CHECK_EQUAL(PLAY_READING, test.history[0].reading);
    CHECK_EQUAL(1260, test.history[0].time_ms);
    CHECK_EQUAL(UP_READING, test.history[1].reading);
    CHECK_EQUAL(1200, test.history[1].time_ms);
    CHECK_EQUAL(PLAY_READING, test.history[2].reading);
    CHECK_EQUAL(IDLE_READING, test.history[3].reading);
    CHECK_EQUAL(PLAY_READING, test.reading);
}

static void the_history_keeps_the_last_six_changes(void)
{
    open_test();
    for (uint32_t index = 0; index < 10; index++) {
        read_at(index % 2 == 0 ? PLAY_READING : DOWN_READING, 2000 + index);
    }
    CHECK_EQUAL(KEY_TEST_HISTORY, test.count);
    CHECK_EQUAL(2009, test.history[0].time_ms);
    CHECK_EQUAL(2004, test.history[KEY_TEST_HISTORY - 1].time_ms);
}

static void ladder_keys_stay_on_the_screen_and_power_leaves(void)
{
    open_test();
    static const Key LADDER[] = {KEY_VOLUME_UP, KEY_VOLUME_DOWN, KEY_PLAY, KEY_MENU, KEY_BACK};
    for (size_t index = 0; index < sizeof LADDER / sizeof LADDER[0]; index++) {
        for (int gesture = KEY_GESTURE_PRESS; gesture <= KEY_GESTURE_REPEAT; gesture++) {
            KeyEvent event = {.key = LADDER[index], .gesture = (KeyGesture)gesture};
            CHECK_EQUAL(SCREEN_ACTION_NONE, test.screen.handle_key(&test, event, 3000).type);
            CHECK_EQUAL(LADDER[index], test.last.key);
        }
    }
    KeyEvent power = {.key = KEY_POWER, .gesture = KEY_GESTURE_PRESS};
    CHECK_EQUAL(SCREEN_ACTION_POP, test.screen.handle_key(&test, power, 3000).type);
    CHECK_EQUAL(SCREEN_KEYS_TEST, test.screen.keys);
}

static void entering_again_starts_a_new_history(void)
{
    open_test();
    read_at(UP_READING, 1100);
    test.screen.handle_key(&test, (KeyEvent){.key = KEY_MENU}, 1100);
    test.screen.enter(&test, 5000);
    CHECK_EQUAL(1, test.count);
    CHECK(!test.has_last);
}

static int needed_height(void)
{
    int width = theme_content_area(&theme).width - 2 * theme.metrics.padding;
    int small = font_line_height(theme.fonts.small);
    int caution = label_wrap_count(theme.fonts.small, i18n_text(STR_KEY_TEST_CAUTION), width);
    int reading =
        font_line_height(theme.fonts.title) * KEY_TEST_SCALE + font_line_height(theme.fonts.body);
    int body = theme_is_portrait(&theme) ? reading + small : (reading > small ? reading : small);
    return body + small * (1 + caution);
}

static void the_key_test_fits_every_text_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            open_test();
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            read_at(PLAY_READING, 36500);
            read_at(DOWN_READING, 36583);
            read_at(PLAY_READING, 36700);
            read_at(IDLE_READING, 36800);
            test.screen.handle_key(
                &test, (KeyEvent){.key = KEY_VOLUME_DOWN, .gesture = KEY_GESTURE_PRESS}, 36583);
            read_at(PLAY_READING, 37000);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            test.screen.draw(&test, &canvas);
            CHECK(needed_height() <= theme_content_area(&theme).height - 2 * theme.metrics.padding);
            screen_snapshot_write(&canvas, "key-test", orientation * TEXT_SIZE_COUNT + size);
        }
    }
}

static void the_key_names_read_plainly(void)
{
    CHECK(strcmp("Power: exit", i18n_text(STR_KEY_TEST_HINT)) == 0);
    CHECK(strcmp("Vol+", i18n_text(STR_KEY_VOLUME_UP)) == 0);
    CHECK(strcmp("Vol-", i18n_text(STR_KEY_VOLUME_DOWN)) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(bands_follow_the_key_decoder),
        TEST_CASE(each_band_change_is_kept_newest_first_with_its_time),
        TEST_CASE(the_history_keeps_the_last_six_changes),
        TEST_CASE(ladder_keys_stay_on_the_screen_and_power_leaves),
        TEST_CASE(entering_again_starts_a_new_history),
        TEST_CASE(the_key_test_fits_every_text_size_and_orientation),
        TEST_CASE(the_key_names_read_plainly),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
