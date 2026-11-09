#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/dac_sequence.h"
#include "device/jack_sense.h"

typedef struct PinRef {
    uint8_t port;
    uint8_t pin;
    bool present;
} PinRef;

enum {
    BOARD_POWER_PAIR_PINS = 2
};

PinRef board_dac_pin(DacPin pin);
uint32_t board_power_pair(PinRef pins[BOARD_POWER_PAIR_PINS]);
JackKind board_jack_kind(bool single_ended_high, bool balanced_high);
