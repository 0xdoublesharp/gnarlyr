#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/audio.h"
#include "screens/usb_dac_menu.h"

static UsbDacMenu menu;

static HeadphoneProfiles profiles;

static void open_menu(AppDacSettings settings)
{
    static Theme theme;
    theme = BOARD_THEME;
    usb_dac_menu_open(&menu, (UsbDacMenuConfig){&theme, settings, &profiles}, 0);
}

static void select_row(int row)
{
    for (int index = 0; index < row; index++) {
        usb_dac_menu_key(&menu, KEY_VOLUME_UP, 0);
    }
}

static void the_menu_lists_volume_gain_output_and_both_other_modes(void)
{
    open_menu((AppDacSettings){35, GAIN_HIGH, OUTPUT_BALANCED, HEADPHONE_PROFILE_NONE});
    CHECK_EQUAL(USB_DAC_ROW_COUNT, menu.model.count(menu.model.context));
    CHECK(strcmp(menu.model.label(&menu, USB_DAC_ROW_VOLUME), i18n_text(STR_VOLUME)) == 0);
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_VOLUME), "35") == 0);
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_GAIN), i18n_text(STR_GAIN_HIGH)) == 0);
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_OUTPUT), i18n_text(STR_JACK_44)) == 0);
    CHECK(strcmp(menu.model.label(&menu, USB_DAC_ROW_FILE_TRANSFER),
                 i18n_text(STR_FILE_TRANSFER)) == 0);
    CHECK(strcmp(menu.model.label(&menu, USB_DAC_ROW_CHARGE_ONLY), i18n_text(STR_CHARGE_ONLY)) ==
          0);
    CHECK(strcmp(menu.model.label(&menu, USB_DAC_ROW_HEADPHONES), i18n_text(STR_HEADPHONES)) == 0);
}

static void play_on_volume_adjusts_it_with_the_volume_keys_until_play(void)
{
    open_menu((AppDacSettings){35, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    CHECK_EQUAL(USB_DAC_ACTION_REDRAW, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK(menu.adjusting);
    CHECK_EQUAL(USB_DAC_ACTION_VOLUME, usb_dac_menu_key(&menu, KEY_VOLUME_UP, 0));
    CHECK_EQUAL(36, menu.settings.volume);
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_VOLUME), "36") == 0);
    CHECK_EQUAL(USB_DAC_ACTION_REDRAW, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK(!menu.adjusting);
    CHECK_EQUAL(USB_DAC_ACTION_REDRAW, usb_dac_menu_key(&menu, KEY_VOLUME_UP, 0));
    CHECK_EQUAL(USB_DAC_ROW_GAIN, menu.view.selected);
}

static void the_volume_stays_within_the_dac_range(void)
{
    open_menu(
        (AppDacSettings){HAL_AUDIO_VOLUME_MAX, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    usb_dac_menu_key(&menu, KEY_PLAY, 0);
    CHECK_EQUAL(USB_DAC_ACTION_NONE, usb_dac_menu_key(&menu, KEY_VOLUME_UP, 0));
    CHECK_EQUAL(HAL_AUDIO_VOLUME_MAX, menu.settings.volume);
    open_menu((AppDacSettings){0, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    usb_dac_menu_key(&menu, KEY_PLAY, 0);
    CHECK_EQUAL(USB_DAC_ACTION_NONE, usb_dac_menu_key(&menu, KEY_VOLUME_DOWN, 0));
    CHECK_EQUAL(0, menu.settings.volume);
}

static void play_cycles_gain_and_output(void)
{
    open_menu((AppDacSettings){35, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    select_row(USB_DAC_ROW_GAIN);
    CHECK_EQUAL(USB_DAC_ACTION_GAIN, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK_EQUAL(GAIN_HIGH, menu.settings.gain);
    CHECK_EQUAL(USB_DAC_ACTION_GAIN, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK_EQUAL(GAIN_LOW, menu.settings.gain);
    select_row(1);
    for (int step = 1; step <= 3; step++) {
        CHECK_EQUAL(USB_DAC_ACTION_OUTPUT, usb_dac_menu_key(&menu, KEY_PLAY, 0));
        CHECK_EQUAL(step % 3, menu.settings.output);
    }
}

static void play_on_headphones_cycles_off_and_every_profile(void)
{
    profiles = (HeadphoneProfiles){.count = 2, .current = HEADPHONE_PROFILE_NONE};
    profiles.slots[0].name[0] = 'A';
    open_menu((AppDacSettings){35, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_HEADPHONES), i18n_text(STR_OFF)) == 0);
    select_row(USB_DAC_ROW_HEADPHONES);
    CHECK_EQUAL(USB_DAC_ACTION_HEADPHONES, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK_EQUAL(0, menu.settings.headphones);
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_HEADPHONES), "A") == 0);
    CHECK_EQUAL(USB_DAC_ACTION_HEADPHONES, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK_EQUAL(1, menu.settings.headphones);
    CHECK(strcmp(menu.model.detail(&menu, USB_DAC_ROW_HEADPHONES), "Profile 2") == 0);
    CHECK_EQUAL(USB_DAC_ACTION_HEADPHONES, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, menu.settings.headphones);
    profiles = (HeadphoneProfiles){.current = HEADPHONE_PROFILE_NONE};
}

static void without_profiles_the_headphones_row_stays_off(void)
{
    profiles = (HeadphoneProfiles){.current = HEADPHONE_PROFILE_NONE};
    open_menu((AppDacSettings){35, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    select_row(USB_DAC_ROW_HEADPHONES);
    CHECK_EQUAL(USB_DAC_ACTION_NONE, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, menu.settings.headphones);
}

static void the_mode_rows_ask_to_switch(void)
{
    open_menu((AppDacSettings){35, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    select_row(USB_DAC_ROW_FILE_TRANSFER);
    CHECK_EQUAL(USB_DAC_ACTION_FILE_TRANSFER, usb_dac_menu_key(&menu, KEY_PLAY, 0));
    select_row(1);
    CHECK_EQUAL(USB_DAC_ACTION_CHARGE_ONLY, usb_dac_menu_key(&menu, KEY_PLAY, 0));
}

static void back_or_menu_closes_and_moving_wraps(void)
{
    open_menu((AppDacSettings){35, GAIN_LOW, OUTPUT_AUTO, HEADPHONE_PROFILE_NONE});
    usb_dac_menu_key(&menu, KEY_VOLUME_DOWN, 0);
    CHECK_EQUAL(USB_DAC_ROW_CHARGE_ONLY, menu.view.selected);
    CHECK_EQUAL(USB_DAC_ACTION_CLOSE, usb_dac_menu_key(&menu, KEY_BACK, 0));
    CHECK_EQUAL(USB_DAC_ACTION_CLOSE, usb_dac_menu_key(&menu, KEY_MENU, 0));
}

static void every_row_is_visible_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            CHECK(usb_dac_menu_rows(&theme) >= 3);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_menu_lists_volume_gain_output_and_both_other_modes),
        TEST_CASE(play_on_volume_adjusts_it_with_the_volume_keys_until_play),
        TEST_CASE(the_volume_stays_within_the_dac_range),
        TEST_CASE(play_cycles_gain_and_output),
        TEST_CASE(play_on_headphones_cycles_off_and_every_profile),
        TEST_CASE(without_profiles_the_headphones_row_stays_off),
        TEST_CASE(the_mode_rows_ask_to_switch),
        TEST_CASE(back_or_menu_closes_and_moving_wraps),
        TEST_CASE(every_row_is_visible_at_every_size_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
