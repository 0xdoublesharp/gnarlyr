#include "check.h"
#include "device/board_pins.h"

static void the_mini_keeps_mute_on_gpio_2_0_and_the_dac_reset_on_gpio_2_1(void)
{
    PinRef mute = board_dac_pin(BOARD_KIND_MINI, DAC_PIN_MUTE);
    PinRef reset = board_dac_pin(BOARD_KIND_MINI, DAC_PIN_RESET);
    CHECK(mute.present);
    CHECK_EQUAL(2, mute.port);
    CHECK_EQUAL(0, mute.pin);
    CHECK(reset.present);
    CHECK_EQUAL(2, reset.port);
    CHECK_EQUAL(1, reset.pin);
}

static void the_echo_mutes_on_gpio_2_1_and_has_no_dac_reset_pin(void)
{
    PinRef mute = board_dac_pin(BOARD_KIND_ECHO, DAC_PIN_MUTE);
    CHECK(mute.present);
    CHECK_EQUAL(2, mute.port);
    CHECK_EQUAL(1, mute.pin);
    CHECK(!board_dac_pin(BOARD_KIND_ECHO, DAC_PIN_RESET).present);
}

static void the_enable_and_amp_pins_are_the_same_on_both_boards(void)
{
    DacPin shared[] = {DAC_PIN_ENABLE_A, DAC_PIN_ENABLE_B, DAC_PIN_AMP};
    for (int index = 0; index < 3; index++) {
        PinRef mini = board_dac_pin(BOARD_KIND_MINI, shared[index]);
        PinRef echo = board_dac_pin(BOARD_KIND_ECHO, shared[index]);
        CHECK(echo.present);
        CHECK_EQUAL(mini.port, echo.port);
        CHECK_EQUAL(mini.pin, echo.pin);
    }
    CHECK_EQUAL(14, board_dac_pin(BOARD_KIND_ECHO, DAC_PIN_ENABLE_A).pin);
    CHECK_EQUAL(2, board_dac_pin(BOARD_KIND_ECHO, DAC_PIN_ENABLE_B).pin);
    CHECK_EQUAL(1, board_dac_pin(BOARD_KIND_ECHO, DAC_PIN_AMP).port);
    CHECK_EQUAL(4, board_dac_pin(BOARD_KIND_ECHO, DAC_PIN_AMP).pin);
}

static void the_echo_has_a_power_pair_on_gpio_0_14_and_2_0(void)
{
    PinRef pins[BOARD_POWER_PAIR_PINS];
    CHECK_EQUAL(2, board_power_pair(BOARD_KIND_ECHO, pins));
    CHECK_EQUAL(0, pins[0].port);
    CHECK_EQUAL(14, pins[0].pin);
    CHECK_EQUAL(2, pins[1].port);
    CHECK_EQUAL(0, pins[1].pin);
}

static void the_mini_has_no_power_pair(void)
{
    PinRef pins[BOARD_POWER_PAIR_PINS];
    CHECK_EQUAL(0, board_power_pair(BOARD_KIND_MINI, pins));
}

static void the_mini_treats_a_low_4_4_pin_as_plugged(void)
{
    CHECK_EQUAL(JACK_BALANCED, board_jack_kind(BOARD_KIND_MINI, false, false));
    CHECK_EQUAL(JACK_BALANCED, board_jack_kind(BOARD_KIND_MINI, true, false));
    CHECK_EQUAL(JACK_SINGLE_ENDED, board_jack_kind(BOARD_KIND_MINI, true, true));
    CHECK_EQUAL(JACK_NONE, board_jack_kind(BOARD_KIND_MINI, false, true));
}

static void the_echo_treats_a_high_4_4_pin_as_plugged(void)
{
    CHECK_EQUAL(JACK_BALANCED, board_jack_kind(BOARD_KIND_ECHO, false, true));
    CHECK_EQUAL(JACK_BALANCED, board_jack_kind(BOARD_KIND_ECHO, true, true));
    CHECK_EQUAL(JACK_SINGLE_ENDED, board_jack_kind(BOARD_KIND_ECHO, true, false));
    CHECK_EQUAL(JACK_NONE, board_jack_kind(BOARD_KIND_ECHO, false, false));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_mini_keeps_mute_on_gpio_2_0_and_the_dac_reset_on_gpio_2_1),
        TEST_CASE(the_echo_mutes_on_gpio_2_1_and_has_no_dac_reset_pin),
        TEST_CASE(the_enable_and_amp_pins_are_the_same_on_both_boards),
        TEST_CASE(the_echo_has_a_power_pair_on_gpio_0_14_and_2_0),
        TEST_CASE(the_mini_has_no_power_pair),
        TEST_CASE(the_mini_treats_a_low_4_4_pin_as_plugged),
        TEST_CASE(the_echo_treats_a_high_4_4_pin_as_plugged),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
