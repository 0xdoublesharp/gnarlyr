#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings/settings.h"

enum {
    EQUALIZER_SLOT_COUNT = 8,
    EQUALIZER_SLOTS_FILE_SIZE = 88
};

typedef struct EqualizerSlots {
    int8_t gains[EQUALIZER_SLOT_COUNT][SETTINGS_EQUALIZER_BANDS];
    uint8_t used;
} EqualizerSlots;

bool equalizer_slots_used(const EqualizerSlots *slots, int slot);
void equalizer_slots_put(EqualizerSlots *slots, int slot, const int8_t *gains);
void equalizer_slots_encode(const EqualizerSlots *slots, uint8_t *bytes);
bool equalizer_slots_decode(const uint8_t *bytes, uint32_t length, EqualizerSlots *slots);
