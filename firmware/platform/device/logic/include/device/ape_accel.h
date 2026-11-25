#pragma once

#include "device/ape_track.h"
#include "device/flac_accel.h"
#include "filter.h"

typedef struct ApeAccel {
    const AccelBus *bus;
    uint32_t verified;
    int32_t output[2][APE_CHUNK_SAMPLES];
} ApeAccel;

void ape_accel_start(ApeAccel *accel, const AccelBus *bus);
bool ape_accel_filter(void *context, DemacFilterJob job);
void ape_accel_test_input(int32_t *samples, uint32_t count, uint32_t offset, uint32_t channel);
