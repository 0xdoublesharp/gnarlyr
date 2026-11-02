#include "device/board_pins.h"

static const PinRef ECHO_DAC_PINS[] = {
    [DAC_PIN_MUTE] = {2, 1, true},     [DAC_PIN_ENABLE_A] = {2, 14, true},
    [DAC_PIN_ENABLE_B] = {2, 2, true}, [DAC_PIN_RESET] = {0, 0, false},
    [DAC_PIN_AMP] = {1, 4, true},
};

static const PinRef ECHO_POWER_PAIR[BOARD_POWER_PAIR_PINS] = {{0, 14, true}, {2, 0, true}};

PinRef board_dac_pin(DacPin pin)
{
    return ECHO_DAC_PINS[pin];
}

uint32_t board_power_pair(PinRef pins[BOARD_POWER_PAIR_PINS])
{
    for (uint32_t index = 0; index < BOARD_POWER_PAIR_PINS; index++) {
        pins[index] = ECHO_POWER_PAIR[index];
    }
    return BOARD_POWER_PAIR_PINS;
}

JackKind board_jack_kind(bool single_ended_high, bool balanced_high)
{
    if (balanced_high) {
        return JACK_BALANCED;
    }
    return single_ended_high ? JACK_SINGLE_ENDED : JACK_NONE;
}
