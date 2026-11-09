#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/alac.h"
#include "device/aac_decoder.h"
#include "device/core1_stream.h"
#include "device/m4a_stream.h"

enum {
    M4A_CONFIG_RECORD_BYTES = 40,
    M4A_HEAP_BYTES = 0x10000,
    M4A_ALAC_PACKET_SLACK = 64
};

typedef struct Core1M4a {
    AacDecoder *aac;
    AlacDecoder *alac;
    uint8_t *packet;
    uint32_t packet_capacity;
    FlacInput source;
    uint32_t position;
    uint32_t next_sample;
    uint8_t codec;
} Core1M4a;

Core1Decoder core1_m4a_decoder(Core1M4a *m4a);
