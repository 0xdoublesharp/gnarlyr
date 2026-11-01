#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/flac.h"

enum {
    MP4_CONFIG_BYTES = 32,
    MP4_SIZE_WINDOW = 64
};

typedef enum Mp4Codec {
    MP4_CODEC_AAC = 1,
    MP4_CODEC_ALAC = 2
} Mp4Codec;

typedef struct Mp4Track {
    uint8_t codec;
    uint8_t channels;
    uint8_t bits;
    uint8_t config_size;
    uint8_t config[MP4_CONFIG_BYTES];
    uint32_t sample_rate;
    uint32_t frame_samples;
    uint32_t total_samples;
    uint32_t delay;
    uint32_t valid_samples;
    uint32_t sample_count;
    uint32_t audio_bytes;
    uint32_t fixed_size;
    uint32_t sizes_at;
    uint32_t chunks_at;
    uint32_t chunk_count;
    uint32_t runs_at;
    uint32_t run_count;
    bool wide_offsets;
} Mp4Track;

typedef struct Mp4Cursor {
    uint32_t sample;
    uint32_t chunk;
    uint32_t in_chunk;
    uint32_t chunk_samples;
    uint32_t run;
    uint32_t run_end;
    uint32_t offset;
    uint32_t window_first;
    uint32_t window_count;
    uint32_t window[MP4_SIZE_WINDOW];
} Mp4Cursor;

bool mp4_signature(const uint8_t bytes[8]);
bool mp4_probe(FlacInput input, Mp4Track *track);
bool mp4_cursor_seek(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t sample);
bool mp4_cursor_next(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t *offset,
                     uint32_t *size);
