#include "device/audio_engine_stage.h"

#include "core/seam.h"

enum {
    WORD_SHIFT = 8
};

static bool fading_out(AudioEngine *engine)
{
    if (engine->fade_out.begun) {
        return true;
    }
    bool wanted = engine->has_next && engine->next_fade_ms != 0 && engine->segment_count > 0 &&
                  !audio_engine_same_text(engine->segments[engine->segment_count - 1].path,
                                          engine->next_path);
    engine->fade_out = wanted ? audio_engine_fade_ramp(engine, engine->next_fade_ms, 0)
                              : (AudioFadeRamp){.active = false};
    return engine->fade_out.active;
}

static bool remaining_output(const AudioEngine *engine, int64_t *remaining)
{
    uint32_t end = engine->decode_limit != 0 ? engine->decode_limit
                                             : engine->decode_track.stream.total_samples;
    if (end == 0) {
        return false;
    }
    int64_t input = (int64_t)end - engine->frame.first_sample - engine->pending_offset;
    if (engine->stretching) {
        input = audio_tempo_stage_pending(input > 0 ? (uint32_t)input : 0);
    }
    *remaining =
        input * audio_engine_output_factor(engine) + audio_resample_pending(&engine->resample);
    return true;
}

CORE_SEAM void audio_engine_stage_shape(AudioEngine *engine, uint32_t first, bool tail)
{
    uint32_t frames = engine->produced - first;
    if (frames == 0) {
        return;
    }
    AudioFadeBlock block = {.ring = engine->config.ring,
                            .ring_frames = audio_engine_ring_frames(engine),
                            .first = first,
                            .frames = frames};
    if (engine->fade_in.active) {
        audio_fade_in(block, &engine->fade_in);
    }
    int64_t remaining = audio_engine_pending_output(engine);
    if (tail ? engine->fade_out.active
             : fading_out(engine) && remaining_output(engine, &remaining)) {
        audio_fade_out(block, &engine->fade_out, remaining);
    }
}

static void pull_into_ring(AudioEngine *engine)
{
    uint32_t *words = NULL;
    uint32_t room = 0;
    while ((room = audio_engine_ring_span(engine, &words)) > 0) {
        uint32_t made = audio_tempo_stage_pull((int32_t *)words, room, WORD_SHIFT);
        engine->produced += made;
        if (made < room) {
            return;
        }
    }
}

static void pull_through_resampler(AudioEngine *engine)
{
    int32_t stage[AUDIO_ENGINE_WORDS_PER_FRAME * (AUDIO_RESAMPLE_HISTORY - AUDIO_RESAMPLE_LEFT)];
    uint32_t room = 0;
    audio_engine_render_resampled(engine);
    while (audio_engine_free_space(engine) > 0 &&
           (room = audio_resample_room(&engine->resample)) > 0) {
        uint32_t made = audio_tempo_stage_pull(stage, room, 0);
        if (made == 0) {
            return;
        }
        audio_resample_push(&engine->resample, stage, made);
        audio_engine_render_resampled(engine);
    }
}

CORE_SEAM uint32_t audio_engine_stage_stretch(AudioEngine *engine, const int32_t *stage,
                                              uint32_t frames)
{
    if (engine->resample.factor > 1) {
        pull_through_resampler(engine);
    } else {
        pull_into_ring(engine);
    }
    if (frames == 0 || audio_engine_free_space(engine) == 0) {
        return 0;
    }
    return audio_tempo_stage_push(stage, frames);
}
