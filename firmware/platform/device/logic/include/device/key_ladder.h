#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

enum {
    KEY_LADDER_IDLE_READING = 1023
};

bool key_ladder_decode(uint16_t reading, Key *key);
uint8_t key_ladder_mask(uint16_t reading);
uint8_t key_ladder_held_mask(uint16_t reading, uint8_t previous);
uint16_t key_ladder_reading(uint8_t held);
