#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings/settings.h"

typedef struct EqualizerFitPoint {
    uint16_t hz;
    int16_t hundredths;
} EqualizerFitPoint;

typedef struct EqualizerFit {
    EqualizerFitPoint below[SETTINGS_EQUALIZER_BANDS];
    EqualizerFitPoint above[SETTINGS_EQUALIZER_BANDS];
    uint32_t whole;
    uint32_t fraction;
    uint32_t points;
    int32_t first;
    uint8_t fraction_digits;
    bool in_number;
    bool in_fraction;
    bool negative;
    bool minus;
    bool have_first;
    bool line_start;
    bool comment;
} EqualizerFit;

void equalizer_fit_start(EqualizerFit *fit);
void equalizer_fit_point(EqualizerFit *fit, uint32_t hz, int32_t hundredths);
void equalizer_fit_text(EqualizerFit *fit, const char *text, uint32_t length);
bool equalizer_fit_finish(EqualizerFit *fit, int8_t gains[SETTINGS_EQUALIZER_BANDS]);
