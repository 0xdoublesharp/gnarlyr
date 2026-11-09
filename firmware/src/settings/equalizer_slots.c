#include "settings/equalizer_slots.h"

#include "core/hash.h"

enum {
    MAGIC_SIZE = 4,
    SLOT_SIZE = SETTINGS_EQUALIZER_BANDS,
    CHECKSUM_FIELD = MAGIC_SIZE + EQUALIZER_SLOT_COUNT * SLOT_SIZE,
    EMPTY = 0xFF
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'Q', 'S', 'L'};

bool equalizer_slots_used(const EqualizerSlots *slots, int slot)
{
    return slot >= 0 && slot < EQUALIZER_SLOT_COUNT && (slots->used >> slot & 1) != 0;
}

void equalizer_slots_put(EqualizerSlots *slots, int slot, const int8_t *gains)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        slots->gains[slot][band] = gains[band];
    }
    slots->used |= (uint8_t)(1 << slot);
}

void equalizer_slots_encode(const EqualizerSlots *slots, uint8_t *bytes)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        bytes[index] = MAGIC[index];
    }
    for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
        for (int band = 0; band < SLOT_SIZE; band++) {
            bytes[MAGIC_SIZE + slot * SLOT_SIZE + band] =
                equalizer_slots_used(slots, slot)
                    ? (uint8_t)(slots->gains[slot][band] + SETTINGS_EQUALIZER_GAIN_MAX)
                    : EMPTY;
        }
    }
    hash_store(bytes + CHECKSUM_FIELD, hash_update(HASH_START, bytes, CHECKSUM_FIELD));
}

static bool slot_valid(const uint8_t *bytes)
{
    bool empty = bytes[0] == EMPTY;
    for (int band = 0; band < SLOT_SIZE; band++) {
        bool fits = bytes[band] <= 2 * SETTINGS_EQUALIZER_GAIN_MAX;
        if (empty ? bytes[band] != EMPTY : !fits) {
            return false;
        }
    }
    return true;
}

bool equalizer_slots_decode(const uint8_t *bytes, uint32_t length, EqualizerSlots *slots)
{
    bool valid =
        length >= EQUALIZER_SLOTS_FILE_SIZE &&
        hash_load(bytes + CHECKSUM_FIELD) == hash_update(HASH_START, bytes, CHECKSUM_FIELD);
    for (int index = 0; valid && index < MAGIC_SIZE; index++) {
        valid = bytes[index] == MAGIC[index];
    }
    *slots = (EqualizerSlots){0};
    for (int slot = 0; valid && slot < EQUALIZER_SLOT_COUNT; slot++) {
        const uint8_t *record = bytes + MAGIC_SIZE + slot * SLOT_SIZE;
        valid = slot_valid(record);
        for (int band = 0; valid && record[0] != EMPTY && band < SLOT_SIZE; band++) {
            slots->gains[slot][band] = (int8_t)(record[band] - SETTINGS_EQUALIZER_GAIN_MAX);
            slots->used |= (uint8_t)(1 << slot);
        }
    }
    if (!valid) {
        *slots = (EqualizerSlots){0};
    }
    return valid;
}
