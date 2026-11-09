#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    AAC_FRAME_SAMPLES = 1024,
    AAC_MAX_PACKET_BYTES = 4096,
    AAC_OUTPUT_BITS = 24
};

typedef struct AacDecoder {
    void *handle;
    int32_t channel[2][AAC_FRAME_SAMPLES];
    uint32_t samples;
    uint8_t channels;
} AacDecoder;

bool aac_decoder_start(AacDecoder *decoder, const uint8_t *config, uint32_t size);
bool aac_decoder_decode(AacDecoder *decoder, uint8_t *packet, uint32_t size);
