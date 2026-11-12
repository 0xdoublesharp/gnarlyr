#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "plug_modes.h"

static const HalBattery CHARGED = {
    .known = true, .percent = 90, .usb_power = true, .charging = true, .millivolts = 4100};
static const HalBattery LOW = {
    .known = true, .percent = 12, .usb_power = true, .charging = true, .millivolts = 3650};
static const HalBattery UNPLUGGED = {.known = true, .percent = 80, .millivolts = 3950};

static PlugChoices choices_at(PlugPlace place, PlugMode saved, HalBattery battery)
{
    return plug_modes_choices(
        (PlugState){.place = place, .saved = saved, .care = 0, .battery = battery});
}

static void check_modes(PlugChoices choices, const PlugMode *expected, int count)
{
    CHECK_EQUAL(count, choices.count);
    for (int index = 0; index < count && index < choices.count; index++) {
        CHECK_EQUAL(expected[index], choices.modes[index]);
    }
}

static void the_prompt_puts_connect_to_computer_first_and_highlights_it(void)
{
    static const PlugMode EXPECTED[] = {PLUG_MODE_COMPUTER, PLUG_MODE_CHARGE, PLUG_MODE_USB_POWER,
                                        PLUG_MODE_DAC};
    PlugChoices choices = choices_at(PLUG_PLACE_PROMPT, PLUG_MODE_ASK, CHARGED);
    check_modes(choices, EXPECTED, 4);
    CHECK_EQUAL(0, choices.cursor);
    CHECK_EQUAL(1, choices.marked);
}

static void switching_later_highlights_and_marks_the_mode_in_use(void)
{
    PlugChoices choices = plug_modes_choices((PlugState){.place = PLUG_PLACE_SWITCH,
                                                         .saved = PLUG_MODE_CHARGE,
                                                         .care = PLUG_CARE_USB,
                                                         .battery = CHARGED});
    CHECK_EQUAL(4, choices.count);
    CHECK_EQUAL(PLUG_MODE_USB_POWER, choices.modes[choices.cursor]);
    CHECK_EQUAL(choices.cursor, choices.marked);
}

static void without_a_cable_usb_power_is_not_offered_and_nothing_is_marked(void)
{
    static const PlugMode EXPECTED[] = {PLUG_MODE_COMPUTER, PLUG_MODE_CHARGE, PLUG_MODE_DAC};
    PlugChoices choices = choices_at(PLUG_PLACE_SWITCH, PLUG_MODE_CHARGE, UNPLUGGED);
    check_modes(choices, EXPECTED, 3);
    CHECK_EQUAL(0, choices.cursor);
    CHECK_EQUAL(PLUG_NOT_MARKED, choices.marked);
}

static void settings_offer_ask_and_every_mode_with_the_saved_one_marked(void)
{
    static const PlugMode EXPECTED[] = {PLUG_MODE_ASK, PLUG_MODE_COMPUTER, PLUG_MODE_CHARGE,
                                        PLUG_MODE_USB_POWER, PLUG_MODE_DAC};
    PlugChoices choices = choices_at(PLUG_PLACE_SETTINGS, PLUG_MODE_DAC, UNPLUGGED);
    check_modes(choices, EXPECTED, 5);
    CHECK_EQUAL(4, choices.cursor);
    CHECK_EQUAL(4, choices.marked);
}

static void a_low_battery_explains_that_usb_power_charges_first(void)
{
    i18n_detach();
    CHECK_EQUAL(STR_PLUG_LOW_LINE, plug_mode_line(PLUG_MODE_USB_POWER, LOW));
    CHECK_EQUAL(STR_PLUG_LOW_LINE,
                plug_mode_line(PLUG_MODE_USB_POWER, (HalBattery){.usb_power = true}));
    CHECK_EQUAL(STR_PLUG_USB_POWER_LINE, plug_mode_line(PLUG_MODE_USB_POWER, CHARGED));
    CHECK_EQUAL(STR_PLUG_CHARGE_LINE, plug_mode_line(PLUG_MODE_CHARGE, LOW));
    CHECK(!plug_modes_battery_ready(LOW));
    CHECK(plug_modes_battery_ready(CHARGED));
}

static void every_mode_has_one_name_one_line_and_one_icon(void)
{
    i18n_detach();
    static const char *const NAMES[PLUG_MODE_COUNT] = {"Ask", "Connect to computer", "Charge",
                                                       "Play on USB power", "USB DAC"};
    static const IconId ICONS[PLUG_MODE_COUNT] = {ICON_USB, ICON_COMPUTER, ICON_BOLT, ICON_PLUG,
                                                  ICON_SPEAKER};
    for (int mode = 0; mode < PLUG_MODE_COUNT; mode++) {
        CHECK(strcmp(NAMES[mode], i18n_text(plug_mode_name((PlugMode)mode))) == 0);
        CHECK_EQUAL(ICONS[mode], plug_mode_icon((PlugMode)mode));
        CHECK(i18n_text(plug_mode_line((PlugMode)mode, CHARGED))[0] != '\0');
    }
}

static void the_saved_mode_round_trips_through_the_settings(void)
{
    for (int mode = 0; mode < PLUG_MODE_COUNT; mode++) {
        Settings settings = settings_defaults();
        plug_mode_save(&settings, (PlugMode)mode);
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&settings, bytes);
        Settings loaded = settings_defaults();
        CHECK(settings_decode(bytes, sizeof bytes, &loaded));
        CHECK_EQUAL(mode, plug_mode_saved(&loaded));
    }
}

static void play_on_usb_power_is_charge_only_with_charging_held_off(void)
{
    Settings settings = settings_defaults();
    plug_mode_save(&settings, PLUG_MODE_USB_POWER);
    CHECK_EQUAL(USB_CONNECT_CHARGE, settings.usb_connect);
    CHECK_EQUAL(PLUG_CARE_USB, settings.battery_care);
    plug_mode_save(&settings, PLUG_MODE_DAC);
    CHECK_EQUAL(USB_CONNECT_DAC, settings.usb_connect);
    CHECK_EQUAL(PLUG_CARE_FULL, settings.battery_care);
}

static void the_default_is_to_ask(void)
{
    Settings settings = settings_defaults();
    CHECK_EQUAL(PLUG_MODE_ASK, plug_mode_saved(&settings));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_prompt_puts_connect_to_computer_first_and_highlights_it),
        TEST_CASE(switching_later_highlights_and_marks_the_mode_in_use),
        TEST_CASE(without_a_cable_usb_power_is_not_offered_and_nothing_is_marked),
        TEST_CASE(settings_offer_ask_and_every_mode_with_the_saved_one_marked),
        TEST_CASE(a_low_battery_explains_that_usb_power_charges_first),
        TEST_CASE(every_mode_has_one_name_one_line_and_one_icon),
        TEST_CASE(the_saved_mode_round_trips_through_the_settings),
        TEST_CASE(play_on_usb_power_is_charge_only_with_charging_held_off),
        TEST_CASE(the_default_is_to_ask),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
