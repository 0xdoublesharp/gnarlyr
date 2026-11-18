#include "device/audio_engine_stage.h"

#include "core/seam.h"

static void clear_frames(uint32_t *words, uint32_t frames)
{
    for (uint32_t index = 0; index < AUDIO_ENGINE_WORDS_PER_FRAME * frames; index++) {
        words[index] = 0;
    }
}

CORE_SEAM void audio_engine_settle(AudioEngine *engine, uint32_t previous)
{
    audio_skip_settle(&engine->effects.skip, engine->segments, engine->segment_count,
                      engine->consumed);
    uint32_t frames = audio_engine_ring_frames(engine);
    uint32_t played = engine->consumed - previous;
    if ((int32_t)played <= 0 || frames == 0) {
        return;
    }
    played = played < frames ? played : frames;
    uint32_t start = previous % frames;
    uint32_t piece = frames - start < played ? frames - start : played;
    clear_frames(&engine->config.ring[AUDIO_ENGINE_WORDS_PER_FRAME * start], piece);
    clear_frames(engine->config.ring, played - piece);
}
