#include "device/audio_fade.h"

#include "core/seam.h"

enum {
    SAMPLE_SHIFT = 8,
    CHANNELS = 2,
    POSITION_SHIFT = 17,
    ROUNDING = 1 << (AUDIO_FADE_SHIFT - 1)
};

static uint32_t smooth_gain(int32_t position, AudioFadeSpan span)
{
    if (position <= 0) {
        return 0;
    }
    if ((uint32_t)position >= span.length) {
        return AUDIO_FADE_UNITY;
    }
    uint32_t x = (uint32_t)(((uint64_t)(uint32_t)position * span.reciprocal) >> POSITION_SHIFT);
    uint32_t square = (x * x) >> AUDIO_FADE_SHIFT;
    return (square * (3u * AUDIO_FADE_UNITY - 2u * x)) >> AUDIO_FADE_SHIFT;
}

static uint32_t scaled(uint32_t word, uint32_t gain)
{
    int32_t sample = (int32_t)word >> SAMPLE_SHIFT;
    int32_t value = (int32_t)(((int64_t)sample * gain + ROUNDING) >> AUDIO_FADE_SHIFT);
    return (uint32_t)value << SAMPLE_SHIFT;
}

void audio_fade_run(uint32_t *words, uint32_t frames, AudioFadeSpan span)
{
    int32_t position = span.first;
    for (uint32_t index = 0; index < frames; index++) {
        uint32_t gain = smooth_gain(position, span);
        uint32_t *frame = &words[index * CHANNELS];
        frame[0] = scaled(frame[0], gain);
        frame[1] = scaled(frame[1], gain);
        position += span.step;
    }
}

static void fade_frames(AudioFadeBlock block, uint32_t skip, uint32_t frames, AudioFadeSpan span)
{
    uint32_t start = (block.first + skip) % block.ring_frames;
    uint32_t piece = block.ring_frames - start;
    piece = piece < frames ? piece : frames;
    audio_fade_run(&block.ring[CHANNELS * start], piece, span);
    if (piece < frames) {
        span.first += span.step * (int32_t)piece;
        audio_fade_run(block.ring, frames - piece, span);
    }
}

CORE_SEAM void audio_fade_in(AudioFadeBlock block, AudioFadeRamp *ramp)
{
    int64_t at = (int32_t)(block.first - ramp->start);
    uint32_t before = at >= 0 ? 0 : -at < block.frames ? (uint32_t)-at : block.frames;
    AudioFadeSpan span = {
        .first = (int32_t)-at, .step = -1, .length = ramp->length, .reciprocal = ramp->reciprocal};
    fade_frames(block, 0, before, span);
    at += before;
    int64_t left = (int64_t)ramp->length - at;
    uint32_t after = block.frames - before;
    if (left > 0 && after > 0) {
        span.first = (int32_t)at;
        span.step = 1;
        fade_frames(block, before, left < after ? (uint32_t)left : after, span);
    }
    ramp->active = left > after;
}

CORE_SEAM void audio_fade_out(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining)
{
    int64_t at = remaining + block.frames;
    int64_t unity = at - (int64_t)ramp->length + 1;
    uint32_t skip = unity <= 0 ? 0 : unity < block.frames ? (uint32_t)unity : block.frames;
    if (skip == block.frames) {
        return;
    }
    at -= skip;
    fade_frames(block, skip, block.frames - skip,
                (AudioFadeSpan){.first = at < INT32_MIN ? INT32_MIN : (int32_t)at,
                                .step = -1,
                                .length = ramp->length,
                                .reciprocal = ramp->reciprocal});
    ramp->begun = true;
}
