#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    ALAC_COOKIE_BYTES = 24,
    ALAC_MAX_CHANNELS = 2,
    ALAC_MAX_FRAME_SAMPLES = 4096,
    ALAC_MAX_KB = 24
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

static inline bool alac_config_supported(const AlacConfig *config)
{
    bool depth_ok = config->bit_depth == 16 || config->bit_depth == 20 || config->bit_depth == 24;
    bool channels_ok = config->channels == 1 || config->channels == 2;
    bool frame_ok = config->frame_length >= 1 && config->frame_length <= ALAC_MAX_FRAME_SAMPLES;
    return depth_ok && channels_ok && frame_ok && config->kb <= ALAC_MAX_KB;
}

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
