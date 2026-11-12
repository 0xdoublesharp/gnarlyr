#pragma once

#include "core/parametric.h"
#include "playback/store.h"
#include "settings/sound_shaping.h"

enum {
    PARAMETRIC_SLOT_COUNT = 8,
    PARAMETRIC_RECORD_BYTES = 84,
    PARAMETRIC_LEGACY_BYTES = 4 + 9 * PARAMETRIC_RECORD_BYTES,
    PARAMETRIC_PAYLOAD_BYTES = PARAMETRIC_LEGACY_BYTES + 9 * SOUND_SHAPING_BYTES,
    PARAMETRIC_BANK_BYTES = 3 * STORE_BLOCK_BYTES,
    PARAMETRIC_FILE_BYTES = 2 * PARAMETRIC_BANK_BYTES
};

typedef struct ParametricBank {
    ParametricEq custom;
    ParametricEq slots[PARAMETRIC_SLOT_COUNT];
    SoundShaping shaping[PARAMETRIC_SLOT_COUNT + 1];
    uint8_t used;
} ParametricBank;

typedef struct ParametricFile {
    StoreFile file;
    uint32_t generation;
    uint8_t bank;
    bool valid;
    bool needs_reload;
} ParametricFile;

bool parametric_bank_encode(const ParametricBank *bank, uint8_t *bytes);
bool parametric_bank_decode(const uint8_t *bytes, ParametricBank *bank);
StoreLoad parametric_file_open(ParametricFile *owner, StoreFile file, ParametricBank *bank);
StoreResult parametric_file_save(ParametricFile *owner, const ParametricBank *bank);
