#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/dac_sequence.h"
#include "device/jack_sense.h"

typedef enum BoardKind {
    BOARD_KIND_MINI,
    BOARD_KIND_ECHO
} BoardKind;

typedef struct PinRef {
    uint8_t port;
    uint8_t pin;
    bool present;
} PinRef;

enum {
    BOARD_POWER_PAIR_PINS = 2
};

PinRef board_dac_pin(BoardKind board, DacPin pin);
uint32_t board_power_pair(BoardKind board, PinRef pins[BOARD_POWER_PAIR_PINS]);
JackKind board_jack_kind(BoardKind board, bool single_ended_high, bool balanced_high);
