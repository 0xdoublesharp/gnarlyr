#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    ALAC_COOKIE_BYTES = 24,
    ALAC_MAX_CHANNELS = 2,
    ALAC_MAX_FRAME_SAMPLES = 4096
};

typedef struct AlacConfig {
    uint32_t frame_length;
    uint32_t sample_rate;
    uint8_t bit_depth;
    uint8_t pb;
    uint8_t mb;
    uint8_t kb;
    uint8_t channels;
    uint16_t max_run;
} AlacConfig;

typedef enum AlacResult {
    ALAC_OK,
    ALAC_END,
    ALAC_BAD_DATA,
    ALAC_UNSUPPORTED
} AlacResult;

typedef struct AlacDecoder {
    AlacConfig config;
    uint32_t sample_count;
    int32_t samples[ALAC_MAX_CHANNELS][ALAC_MAX_FRAME_SAMPLES];
} AlacDecoder;

bool alac_parse_cookie(const uint8_t *bytes, uint32_t length, AlacConfig *config);
void alac_start(AlacDecoder *decoder, const AlacConfig *config);
AlacResult alac_decode_packet(AlacDecoder *decoder, const uint8_t *packet, uint32_t length);
uint32_t alac_samples(const AlacDecoder *decoder);
const int32_t *alac_channel(const AlacDecoder *decoder, int channel);
