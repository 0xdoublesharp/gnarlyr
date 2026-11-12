#pragma once

#include "device/core1_stream.h"

enum {
    DSD_PCM_RATE = 88200,
    DSD_RING_BYTES = 32768,
    DSD_HEADER_BYTES = 32,
    DSD_BLOCK_BYTES = 4096,
    DSD_OUTPUT_FRAMES = 512,
    DSD_SECOND_TAPS = 64,
    DSD_PREROLL = 40
};

typedef struct DsdTrack {
    uint64_t samples;
    uint32_t rate;
    uint32_t channels;
    uint32_t layout;
    uint32_t data_offset;
    uint32_t data_bytes;
} DsdTrack;

typedef struct DsdStream {
    FlacInput file;
    DsdTrack track;
    uint8_t header[DSD_HEADER_BYTES];
    uint32_t first;
    uint32_t bytes;
} DsdStream;

typedef struct DsdWorkspace {
    int32_t table[40][256];
    uint8_t bits[2][64];
    int32_t history[2][2 * DSD_SECOND_TAPS];
    uint8_t packet[2 * DSD_BLOCK_BYTES];
} DsdWorkspace;

typedef struct Core1Dsd {
    StreamRegion region;
    DsdWorkspace *work;
    FlacInput input;
    const int32_t *coefficients;
    const int32_t *cic;
    uint32_t rows;
    uint64_t bit_count;
    uint32_t byte_position;
    uint32_t byte_count;
    uint32_t partial_bits;
    uint32_t generation;
    uint32_t position;
    uint32_t packet_size;
    uint32_t packet_cursor;
    uint32_t packet_frames;
    uint32_t channels;
    uint32_t layout;
    uint32_t ratio;
    uint32_t shift;
    uint32_t stage_position;
    uint32_t stage_count;
    uint32_t warmup;
    uint32_t remaining;
    uint32_t first;
    uint32_t staged;
    uint32_t slot;
    bool configured;
} Core1Dsd;

bool dsd_probe(FlacInput input, DsdTrack *track);
bool dsd_stream_start(DsdStream *stream, FlacInput input, uint32_t start_ms, AudioTrackInfo *track);
bool dsd_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);
Core1Decoder core1_dsd_decoder(Core1Dsd *dsd, StreamRegion region);
bool core1_open_dsd(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
