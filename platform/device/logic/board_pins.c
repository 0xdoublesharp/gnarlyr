#include "device/board_pins.h"

static const PinRef MINI_DAC_PINS[] = {
    [DAC_PIN_MUTE] = {2, 0, true},     [DAC_PIN_ENABLE_A] = {2, 14, true},
    [DAC_PIN_ENABLE_B] = {2, 2, true}, [DAC_PIN_RESET] = {2, 1, true},
    [DAC_PIN_AMP] = {1, 4, true},
};

static const PinRef ECHO_DAC_PINS[] = {
    [DAC_PIN_MUTE] = {2, 1, true},     [DAC_PIN_ENABLE_A] = {2, 14, true},
    [DAC_PIN_ENABLE_B] = {2, 2, true}, [DAC_PIN_RESET] = {0, 0, false},
    [DAC_PIN_AMP] = {1, 4, true},
};

static const PinRef ECHO_POWER_PAIR[BOARD_POWER_PAIR_PINS] = {{0, 14, true}, {2, 0, true}};

PinRef board_dac_pin(BoardKind board, DacPin pin)
{
    return board == BOARD_KIND_ECHO ? ECHO_DAC_PINS[pin] : MINI_DAC_PINS[pin];
}

uint32_t board_power_pair(BoardKind board, PinRef pins[BOARD_POWER_PAIR_PINS])
{
    if (board != BOARD_KIND_ECHO) {
        return 0;
    }
    for (uint32_t index = 0; index < BOARD_POWER_PAIR_PINS; index++) {
        pins[index] = ECHO_POWER_PAIR[index];
    }
    return BOARD_POWER_PAIR_PINS;
}

JackKind board_jack_kind(BoardKind board, bool single_ended_high, bool balanced_high)
{
    if (board == BOARD_KIND_ECHO) {
        if (balanced_high) {
            return JACK_BALANCED;
        }
        return single_ended_high ? JACK_SINGLE_ENDED : JACK_NONE;
    }
    return jack_kind_of(single_ended_high, balanced_high);
}
