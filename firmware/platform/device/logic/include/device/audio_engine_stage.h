#pragma once

#include <stddef.h>

#include "device/audio_engine.h"

enum {
    AUDIO_ENGINE_WORDS_PER_FRAME = 2,
    AUDIO_ENGINE_MILLISECONDS_PER_SECOND = 1000
};

void audio_engine_stage_shape(AudioEngine *engine, uint32_t first, bool tail);
uint32_t audio_engine_stage_stretch(AudioEngine *engine, const int32_t *stage, uint32_t frames);
void audio_engine_render_resampled(AudioEngine *engine);

static inline uint32_t audio_engine_frame_limit(uint32_t end_ms, uint32_t rate)
{
    return (uint32_t)((uint64_t)end_ms * rate / AUDIO_ENGINE_MILLISECONDS_PER_SECOND);
}

static inline uint32_t audio_engine_ring_frames(const AudioEngine *engine)
{
    return engine->config.ring_words / AUDIO_ENGINE_WORDS_PER_FRAME;
}

static inline uint32_t audio_engine_free_space(const AudioEngine *engine)
{
    return audio_engine_ring_frames(engine) - (engine->produced - engine->consumed);
}

static inline uint32_t audio_engine_ring_span(const AudioEngine *engine, uint32_t **words)
{
    uint32_t start = engine->produced % audio_engine_ring_frames(engine);
    uint32_t contiguous = audio_engine_ring_frames(engine) - start;
    uint32_t space = audio_engine_free_space(engine);
    *words = &engine->config.ring[AUDIO_ENGINE_WORDS_PER_FRAME * start];
    return contiguous < space ? contiguous : space;
}

static inline bool audio_engine_same_text(const char *left, const char *right)
{
    size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return left[index] == right[index];
}

static inline AudioFadeRamp audio_engine_fade_ramp(const AudioEngine *engine, uint32_t fade_ms,
                                                   uint32_t start)
{
    uint32_t length = audio_engine_frame_limit(fade_ms, engine->output_rate);
    return (AudioFadeRamp){.start = start,
                           .length = length,
                           .reciprocal = length == 0 ? 0 : UINT32_MAX / length,
                           .active = length > 0};
}

static inline uint32_t audio_engine_output_factor(const AudioEngine *engine)
{
    return engine->resample.factor > 1 ? engine->resample.factor : 1;
}

static inline int64_t audio_engine_pending_output(const AudioEngine *engine)
{
    uint32_t staged = engine->stretching ? audio_tempo_stage_pending(0) : 0;
    return (int64_t)staged * audio_engine_output_factor(engine) +
           audio_resample_pending(&engine->resample);
}
