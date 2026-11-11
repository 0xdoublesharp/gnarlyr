#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings/settings.h"

enum {
    EQUALIZER_RESPONSE_POINTS = 78
};

typedef struct EqualizerResponse {
    int16_t gain_tenth_db[EQUALIZER_RESPONSE_POINTS];
    uint16_t frequency_hz[EQUALIZER_RESPONSE_POINTS];
    int16_t preamp_tenth_db;
    int16_t range_tenth_db;
    bool muted;
} EqualizerResponse;

bool equalizer_response_make(EqualizerResponse *response, const SettingsEqualizerCurve *curve);
uint16_t equalizer_response_frequency(int point);
