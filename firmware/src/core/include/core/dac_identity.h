#pragma once

#include <stdint.h>

enum {
    DAC_IDENTITY_CHIPS = 2,
    DAC_IDENTITY_ID_BYTES = 3,
    DAC_IDENTITY_REVISION = 3,
    DAC_IDENTITY_SUBREVISION = 4,
    DAC_IDENTITY_BYTES = 5,
    DAC_IDENTITY_ALL_READ = (1 << DAC_IDENTITY_BYTES) - 1
};

typedef enum DacIdentitySource {
    DAC_IDENTITY_NOT_READ,
    DAC_IDENTITY_READ,
    DAC_IDENTITY_SIMULATED
} DacIdentitySource;

typedef struct DacIdentity {
    uint8_t source;
    uint8_t read_mask;
    uint8_t bytes[DAC_IDENTITY_BYTES];
} DacIdentity;
