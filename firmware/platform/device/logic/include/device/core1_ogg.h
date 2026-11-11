#pragma once

#include "device/core1_stream.h"
#include "device/ogg_record.h"

typedef struct OggRecord {
    uint32_t bytes;
    uint32_t flags;
    uint64_t granule;
} OggRecord;

Core1DecodeResult core1_ogg_fetch(FlacInput input, uint32_t position, uint8_t *packet,
                                  OggRecord *record);
