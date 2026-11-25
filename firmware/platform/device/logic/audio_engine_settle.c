#include "device/audio_engine_stage.h"

#include "core/seam.h"

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
    audio_engine_clear_words(&engine->config.ring[AUDIO_ENGINE_WORDS_PER_FRAME * start], piece,
                             start, engine->output_dop);
    audio_engine_clear_words(engine->config.ring, played - piece, 0, engine->output_dop);
}
