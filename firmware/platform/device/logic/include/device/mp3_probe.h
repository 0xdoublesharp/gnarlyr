#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/flac.h"

enum {
    MP3_TOC_ENTRIES = 100,
    MP3_SIGNATURE_BYTES = 4,
    MP3_DECODER_DELAY = 529,
    MP3_MAX_TAIL_SAMPLES = 4095 - MP3_DECODER_DELAY,
    MP3_TAIL_CAPACITY = MP3_MAX_TAIL_SAMPLES + 1152,
    MP3_RING_BYTES = 65536
};

typedef struct Mp3Stream {
    uint32_t sample_rate;
    uint8_t channels;
    uint16_t samples_per_frame;
    uint32_t total_samples;
    uint32_t lead_trim;
    uint32_t tail_trim;
    uint32_t audio_start;
    uint32_t audio_end;
    bool has_toc;
    uint8_t toc[MP3_TOC_ENTRIES];
} Mp3Stream;

bool mp3_signature(const uint8_t bytes[MP3_SIGNATURE_BYTES]);
bool mp3_header_valid(const uint8_t *bytes);
bool mp3_probe(FlacInput input, Mp3Stream *stream);
uint32_t mp3_seek_offset(const Mp3Stream *stream, uint32_t sample);
uint32_t mp3_playable_samples(const Mp3Stream *stream);
