#pragma once

#include "device/dac_sequence.h"
#include "device/equalizer.h"

typedef struct DacEqProgram {
    bool enabled;
    uint8_t control1;
    uint8_t control2;
    uint8_t coeffs[54];
} DacEqProgram;

bool dac_eq_from_equalizer(const Equalizer *eq, uint32_t sample_rate, DacEqProgram *out);
bool dac_eq_write(const DacBus *bus, const DacEqProgram *program);
bool dac_eq_disable(const DacBus *bus);
bool dac_wideband_flatness_eligible(uint32_t sample_rate, uint8_t filter, bool hw_eq_enabled);
