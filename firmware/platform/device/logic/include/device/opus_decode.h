#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__arm__) && defined(__GNUC__)
typedef intptr_t OpusDecodeCheckpoint[5];
#else
#include <setjmp.h>
typedef jmp_buf OpusDecodeCheckpoint;
#endif

enum {
    OPUS_DECODE_SCRATCH_BYTES = 32768,
    OPUS_DECODE_MAX_FRAMES = 5760,
    OPUS_DECODE_MAX_PACKET_BYTES = 61440
};

typedef enum OpusDecodeResult {
    OPUS_DECODE_OK,
    OPUS_DECODE_INVALID,
    OPUS_DECODE_BUFFER_SMALL,
    OPUS_DECODE_EXHAUSTED,
    OPUS_DECODE_FAILED,
    OPUS_DECODE_BUSY
} OpusDecodeResult;

typedef enum OpusDecodeMode {
    OPUS_DECODE_UNKNOWN,
    OPUS_DECODE_SILK,
    OPUS_DECODE_HYBRID,
    OPUS_DECODE_CELT
} OpusDecodeMode;

typedef struct OpusDecode {
    void *state;
    uint8_t *scratch;
    uint32_t state_bytes;
    uint32_t scratch_bytes;
    uint32_t scratch_used;
    uint32_t scratch_peak;
    uint32_t rate;
    uint32_t channels;
    OpusDecodeMode mode;
    OpusDecodeResult error;
    bool ready;
    OpusDecodeCheckpoint checkpoint;
} OpusDecode;

typedef struct OpusDecodeInput {
    const uint8_t *packet;
    uint32_t bytes;
    uint32_t frames;
    bool fec;
} OpusDecodeInput;

typedef struct OpusDecodePcm {
    int16_t *samples;
    uint32_t capacity;
    uint32_t frames;
    uint32_t rate;
    uint32_t channels;
    OpusDecodeMode mode;
} OpusDecodePcm;

uint32_t opus_decode_state_bytes(uint32_t channels);
OpusDecodeResult opus_decode_init(OpusDecode *decoder, void *state, uint32_t state_bytes,
                                  void *scratch, uint32_t scratch_bytes, uint32_t rate,
                                  uint32_t channels);
OpusDecodeResult opus_decode_set_gain(OpusDecode *decoder, int32_t gain);
OpusDecodeResult opus_decode_packet(OpusDecode *decoder, OpusDecodeInput input, OpusDecodePcm *pcm);
