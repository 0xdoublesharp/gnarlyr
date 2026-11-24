#pragma once

#include "codec/alac.h"
#include "device/flac_accel.h"

enum {
    ALAC_ACCEL_DMA_WORDS = 2048,
    ALAC_ACCEL_DMA_LINKS = (ALAC_MAX_FRAME_SAMPLES + ALAC_ACCEL_DMA_WORDS - 1) /
        ALAC_ACCEL_DMA_WORDS
};

typedef struct AlacAccelLink {
    uint32_t source;
    uint32_t destination;
    uint32_t next;
    uint32_t control;
    uint32_t length;
    uint32_t source_status;
    uint32_t destination_status;
} AlacAccelLink;

typedef struct AlacAccelOutput {
    int32_t *samples;
    uint32_t capacity;
} AlacAccelOutput;

typedef struct AlacAccel {
    const AccelBus *bus;
    AlacAccelOutput output;
    AlacAccelLink input_dma[ALAC_ACCEL_DMA_LINKS];
    AlacAccelLink output_dma[ALAC_ACCEL_DMA_LINKS];
    bool enabled;
} AlacAccel;

void alac_accel_start(AlacAccel *accel, const AccelBus *bus, int32_t *input, int32_t *output);
AlacLpcHook alac_accel_hook(AlacAccel *accel);
