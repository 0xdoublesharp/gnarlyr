#pragma once

#include "core/i18n.h"
#include "core/parametric.h"

enum {
    SOUND_SHAPING_COUNT = 7,
    SOUND_SHAPING_LIMIT = 5,
    SOUND_SHAPING_BYTES = 9
};

typedef struct SoundShaping {
    int8_t positions[SOUND_SHAPING_COUNT];
    uint8_t base_count;
    bool active;
} SoundShaping;

bool sound_shaping_build(ParametricEq *result, const ParametricEq *base, const SoundShaping *shape);
bool sound_shaping_valid(const SoundShaping *shape, const ParametricEq *eq);
StringId sound_shaping_label(int slider);
StringId sound_shaping_end(int slider, bool positive);
void sound_shaping_words(const SoundShaping *shape, char *text, uint32_t capacity);
