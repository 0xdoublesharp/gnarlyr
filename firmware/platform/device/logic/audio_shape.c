#include "device/audio_engine_stage.h"

enum {
    SLEEP_FADE_MS = 60000
};

void audio_engine_set_sleep_remaining(AudioEngine *engine, uint32_t remaining_ms)
{
    if (!engine->sleep_fading && remaining_ms <= SLEEP_FADE_MS) {
        engine->sleep_fade_ms = remaining_ms != 0 ? remaining_ms : 1;
    }
    engine->sleep_fading = remaining_ms <= SLEEP_FADE_MS;
    engine->sleep_end_at = engine->consumed + audio_engine_frame_limit(
                                                  remaining_ms <= SLEEP_FADE_MS ? remaining_ms : 0,
                                                  engine->output_rate);
}

static bool repeat_seek(AudioEngine *engine, uint32_t sample, uint32_t rate)
{
    uint32_t ms = (uint32_t)((uint64_t)sample * AUDIO_ENGINE_MILLISECONDS_PER_SECOND / rate);
    if (!audio_engine_seek(engine, ms)) {
        engine->repeat_state = 0;
        return false;
    }
    engine->skip_frames = sample;
    engine->segments[0].base_sample = sample;
    return true;
}

uint8_t audio_engine_repeat_mark(AudioEngine *engine, bool clear)
{
    if (engine->segment_count == 0 || engine->state == AUDIO_ENGINE_STOPPED) {
        engine->repeat_state = 0;
        return 0;
    }
    AudioEngineStatus status = audio_engine_status(engine);
    uint32_t sample = status.position_samples;
    if (clear) {
        bool had_loop = engine->repeat_state == 2;
        engine->repeat_state = 0;
        if (had_loop) {
            (void)repeat_seek(engine, sample, status.track.stream.sample_rate);
        }
        return 0;
    }
    if (engine->repeat_state == 0) {
        engine->repeat_a = sample;
        engine->repeat_token = status.token;
        engine->repeat_state = 1;
        return 1;
    }
    if (engine->repeat_state == 1) {
        if (sample <= engine->repeat_a) {
            return 1;
        }
        engine->repeat_b = sample;
        engine->repeat_state = 2;
        sample = engine->repeat_a;
        engine->has_next = false;
    } else {
        engine->repeat_state = 0;
    }
    return repeat_seek(engine, sample, status.track.stream.sample_rate) ? engine->repeat_state : 0;
}
