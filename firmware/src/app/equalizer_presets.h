#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/i18n.h"
#include "parametric_store.h"
#include "settings/equalizer_slots.h"
#include "settings/settings.h"

enum {
    EQUALIZER_PRESET_FILES = 16,
    EQUALIZER_PRESET_NAME_CAPACITY = 28,
    EQUALIZER_PRESET_OPTIONS = EQUALIZER_PRESET_COUNT + EQUALIZER_SLOT_COUNT +
        EQUALIZER_PRESET_FILES
};

extern const char EQUALIZER_SLOTS_PATH[];
extern const StringId EQUALIZER_PRESET_NAMES[EQUALIZER_PRESET_COUNT];

typedef struct EqualizerPresets {
    EqualizerSlots slots;
    ParametricStore *parametric;
    bool trimmed;
    char files[EQUALIZER_PRESET_FILES][EQUALIZER_PRESET_NAME_CAPACITY];
    const char *options[EQUALIZER_PRESET_OPTIONS];
    uint8_t slot_at[EQUALIZER_SLOT_COUNT];
    int slot_count;
    int file_count;
    int option_count;
} EqualizerPresets;

void equalizer_presets_read_slots(EqualizerPresets *presets);
bool equalizer_presets_save_slot(EqualizerPresets *presets, int slot, const int8_t *gains);
void equalizer_presets_scan(EqualizerPresets *presets);
const char *equalizer_presets_slot_label(int slot);
int equalizer_presets_matching_slot(const EqualizerPresets *presets, const int8_t *gains);
int equalizer_presets_slot_option(const EqualizerPresets *presets, int slot);
int equalizer_presets_option_slot(const EqualizerPresets *presets, int option);
bool equalizer_presets_load(EqualizerPresets *presets, int option, int8_t *gains);

bool equalizer_presets_choose(EqualizerPresets *presets, int option, Settings *settings);
bool equalizer_presets_slot_used(const EqualizerPresets *presets, int slot);
