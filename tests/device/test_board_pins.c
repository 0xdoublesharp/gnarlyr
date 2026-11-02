#include "check.h"
#include "device/board_pins.h"

static void the_echo_mutes_on_gpio_2_1_and_has_no_dac_reset_pin(void)
{
    PinRef mute = board_dac_pin(DAC_PIN_MUTE);
    CHECK(mute.present);
    CHECK_EQUAL(2, mute.port);
    CHECK_EQUAL(1, mute.pin);
    CHECK(!board_dac_pin(DAC_PIN_RESET).present);
}

static void the_enable_and_amp_pins_sit_on_their_gpios(void)
{
    CHECK(board_dac_pin(DAC_PIN_ENABLE_A).present);
    CHECK(board_dac_pin(DAC_PIN_ENABLE_B).present);
    CHECK(board_dac_pin(DAC_PIN_AMP).present);
    CHECK_EQUAL(14, board_dac_pin(DAC_PIN_ENABLE_A).pin);
    CHECK_EQUAL(2, board_dac_pin(DAC_PIN_ENABLE_B).pin);
    CHECK_EQUAL(1, board_dac_pin(DAC_PIN_AMP).port);
    CHECK_EQUAL(4, board_dac_pin(DAC_PIN_AMP).pin);
}

static void the_echo_has_a_power_pair_on_gpio_0_14_and_2_0(void)
{
    PinRef pins[BOARD_POWER_PAIR_PINS];
    CHECK_EQUAL(2, board_power_pair(pins));
    CHECK_EQUAL(0, pins[0].port);
    CHECK_EQUAL(14, pins[0].pin);
    CHECK_EQUAL(2, pins[1].port);
    CHECK_EQUAL(0, pins[1].pin);
}

static void the_echo_treats_a_high_4_4_pin_as_plugged(void)
{
    CHECK_EQUAL(JACK_BALANCED, board_jack_kind(false, true));
    CHECK_EQUAL(JACK_BALANCED, board_jack_kind(true, true));
    CHECK_EQUAL(JACK_SINGLE_ENDED, board_jack_kind(true, false));
    CHECK_EQUAL(JACK_NONE, board_jack_kind(false, false));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_echo_mutes_on_gpio_2_1_and_has_no_dac_reset_pin),
        TEST_CASE(the_enable_and_amp_pins_sit_on_their_gpios),
        TEST_CASE(the_echo_has_a_power_pair_on_gpio_0_14_and_2_0),
        TEST_CASE(the_echo_treats_a_high_4_4_pin_as_plugged),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
