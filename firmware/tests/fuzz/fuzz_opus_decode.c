#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/opus_decode.h"
#include "fuzz_source.h"

enum {
    CONTROL_BYTES = 12,
    STATE_BYTES = 32768,
    SCRATCH_BYTES = OPUS_DECODE_SCRATCH_BYTES + 16,
    SAMPLE_WORDS = OPUS_DECODE_MAX_FRAMES * 2 + 2
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint32_t RATES[] = {8000, 12000, 16000, 24000, 48000, 44100};
    static _Alignas(8) uint8_t state[STATE_BYTES + 8];
    static _Alignas(8) uint8_t scratch[SCRATCH_BYTES];
    static int16_t samples[SAMPLE_WORDS];
    uint8_t controls[CONTROL_BYTES] = {0};
    size_t controls_size = size < sizeof controls ? size : sizeof controls;
    memcpy(controls, data, controls_size);
    memset(state, 0, sizeof state);
    memset(scratch, 0, sizeof scratch);
    memset(samples, 0, sizeof samples);
    uint32_t channels = controls[0] % 4;
    uint32_t rate = RATES[controls[1] % (sizeof RATES / sizeof RATES[0])];
    uint32_t state_size = controls[2] == 0 ? STATE_BYTES : (uint32_t)controls[2] * 128u;
    uint32_t scratch_size =
        controls[3] == 0 ? OPUS_DECODE_SCRATCH_BYTES : 8u + (uint32_t)controls[3] * 128u;
    uint32_t state_offset = controls[4] & 1u;
    uint32_t scratch_offset = 8u + (controls[4] >> 1 & 1u);
    OpusDecode decoder;
    OpusDecodeResult initialized =
        opus_decode_init(&decoder, state + state_offset, state_size, scratch + scratch_offset,
                         scratch_size, rate, channels);
    fuzz_require(initialized >= OPUS_DECODE_OK && initialized <= OPUS_DECODE_BUSY);
    if (initialized != OPUS_DECODE_OK) {
        return 0;
    }
    int32_t gain = (int32_t)((uint32_t)controls[5] << 24 | (uint32_t)controls[6] << 16 |
                             (uint32_t)controls[7] << 8 | controls[8]);
    OpusDecodeResult gain_result = opus_decode_set_gain(&decoder, gain);
    fuzz_require(gain_result == OPUS_DECODE_OK || gain_result == OPUS_DECODE_INVALID);
    const uint8_t *packet = size > CONTROL_BYTES ? data + CONTROL_BYTES : data + size;
    uint32_t bytes = size > CONTROL_BYTES ? (uint32_t)(size - CONTROL_BYTES) : 0;
    bool lost = (controls[9] & 1u) != 0;
    uint32_t frames = (uint32_t)controls[10] * (rate / 400u);
    OpusDecodeInput input = {.packet = lost ? NULL : packet,
                             .bytes = lost ? ((controls[9] & 2u) != 0 ? bytes : 0) : bytes,
                             .frames = (controls[9] & 4u) != 0 ? frames : 0,
                             .fec = (controls[9] & 8u) != 0};
    OpusDecodePcm pcm = {.samples = samples,
                         .capacity = (uint32_t)controls[11] * OPUS_DECODE_MAX_FRAMES / UINT8_MAX};
    OpusDecodeResult decoded = opus_decode_packet(&decoder, input, &pcm);
    fuzz_require(decoded >= OPUS_DECODE_OK && decoded <= OPUS_DECODE_BUSY);
    if (decoded == OPUS_DECODE_OK) {
        fuzz_require(pcm.frames > 0 && pcm.frames <= pcm.capacity);
        fuzz_require(pcm.rate == rate && pcm.channels == channels);
    }
    fuzz_require(decoder.scratch_used == 0);
    return 0;
}
