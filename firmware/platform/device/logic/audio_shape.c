#include "device/audio_engine.h"

enum {
    UNITY_GAIN = 65536,
    WORDS_PER_FRAME = 2,
    OUTPUT_BITS = 24,
    SIXTEEN_BITS = 16,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608
};

static uint32_t frame_limit(uint32_t ms, uint32_t rate)
{
    return (uint32_t)((uint64_t)ms * rate / 1000);
}

static AudioFadeRamp fade_ramp(const AudioEngine *engine, uint32_t ms, uint32_t start)
{
    uint32_t length = frame_limit(ms, engine->output_rate);
    return (AudioFadeRamp){.start = start,
                           .length = length,
                           .reciprocal = length == 0 ? 0 : UINT32_MAX / length,
                           .active = length > 0};
}

static bool same_text(const char *left, const char *right)
{
    uint32_t at = 0;
    while (left[at] != 0 && left[at] == right[at]) {
        at++;
    }
    return left[at] == right[at];
}

static bool fading_out(AudioEngine *engine)
{
    if (engine->fade_out.begun) {
        return true;
    }
    bool wanted = engine->has_next && engine->next_fade_ms != 0 && engine->segment_count > 0 &&
                  !same_text(engine->segments[engine->segment_count - 1].path, engine->next_path);
    engine->fade_out =
        wanted ? fade_ramp(engine, engine->next_fade_ms, 0) : (AudioFadeRamp){.active = false};
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
    uint32_t factor = engine->resample.factor > 1 ? engine->resample.factor : 1;
    *remaining = input * factor + audio_resample_pending(&engine->resample);
    return true;
}

void audio_shape_block(AudioEngine *engine, uint32_t first, bool tail)
{
    uint32_t frames = engine->produced - first;
    if (frames == 0) {
        return;
    }
    AudioFadeBlock block = {.ring = engine->config.ring,
                            .ring_frames = engine->config.ring_words / 2,
                            .first = first,
                            .frames = frames};
    if (engine->fade_in.active) {
        audio_fade_in(block, &engine->fade_in);
    }
    int64_t remaining = audio_resample_pending(&engine->resample);
    if (tail ? engine->fade_out.active
             : fading_out(engine) && remaining_output(engine, &remaining)) {
        audio_fade_out(block, &engine->fade_out, remaining);
    }
    if (engine->sleep_fading) {
        AudioFadeRamp sleep = fade_ramp(engine, engine->sleep_fade_ms, 0);
        audio_fade_out(block, &sleep, (int32_t)(engine->sleep_end_at - engine->produced));
    }
}

void audio_engine_set_sleep_remaining(AudioEngine *engine, uint32_t remaining_ms)
{
    if (!engine->sleep_fading && remaining_ms <= 60000) {
        engine->sleep_fade_ms = remaining_ms != 0 ? remaining_ms : 1;
    }
    engine->sleep_fading = remaining_ms <= 60000;
    engine->sleep_end_at = engine->consumed + frame_limit(remaining_ms <= 60000 ? remaining_ms : 0,
                                                          engine->output_rate);
}

uint8_t audio_engine_repeat_mark(AudioEngine *engine, bool clear)
{
    if (engine->segment_count == 0 || engine->state == AUDIO_ENGINE_STOPPED) {
        return 0;
    }
    AudioEngineStatus status = audio_engine_status(engine);
    uint32_t sample = status.position_samples;
    if (engine->repeat_state == 0 && !clear) {
        engine->repeat_a = sample;
        engine->repeat_token = status.token;
        engine->repeat_state = 1;
        return 1;
    }
    if (engine->repeat_state == 2 || clear) {
        engine->repeat_state = 0;
    } else if (sample > engine->repeat_a) {
        engine->repeat_b = sample;
        engine->repeat_state = 2;
        sample = engine->repeat_a;
        engine->has_next = false;
    }
    uint32_t ms = (uint32_t)((uint64_t)sample * 1000 / status.track.stream.sample_rate);
    if (!audio_engine_seek(engine, ms)) {
        engine->repeat_state = 0;
        return 0;
    }
    engine->skip_frames = sample;
    engine->segments[0].base_sample = sample;
    return engine->repeat_state;
}

static int32_t saturate(int32_t sample)
{
    return sample > SAMPLE_MAX ? SAMPLE_MAX : sample < SAMPLE_MIN ? SAMPLE_MIN : sample;
}

static int32_t unity_16(int32_t sample)
{
    return sample > INT16_MAX   ? SAMPLE_MAX
           : sample < INT16_MIN ? SAMPLE_MIN
                                : sample * (1 << (OUTPUT_BITS - SIXTEEN_BITS));
}

typedef struct PcmSpan {
    uint32_t *words;
    const int32_t *left;
    const int32_t *right;
    uint32_t frames;
    int shift;
} PcmSpan;

static void convert_16(const PcmSpan *span)
{
    uint32_t *words = span->words;
    const int32_t *left = span->left;
    const int32_t *right = span->right;
    const int32_t *end = left + span->frames;
    int shift = span->shift;
    if (left == right) {
        for (; left != end; left++, words += WORDS_PER_FRAME) {
            uint32_t word = (uint32_t)unity_16(*left) << shift;
            words[0] = word;
            words[1] = word;
        }
        return;
    }
    for (; left != end; left++, right++, words += WORDS_PER_FRAME) {
        words[0] = (uint32_t)unity_16(*left) << shift;
        words[1] = (uint32_t)unity_16(*right) << shift;
    }
}

static void convert_24(const PcmSpan *span)
{
    uint32_t *words = span->words;
    const int32_t *left = span->left;
    const int32_t *right = span->right;
    const int32_t *end = left + span->frames;
    int shift = span->shift;
    if (left == right) {
        for (; left != end; left++, words += WORDS_PER_FRAME) {
            uint32_t word = (uint32_t)saturate(*left) << shift;
            words[0] = word;
            words[1] = word;
        }
        return;
    }
    for (; left != end; left++, right++, words += WORDS_PER_FRAME) {
        words[0] = (uint32_t)saturate(*left) << shift;
        words[1] = (uint32_t)saturate(*right) << shift;
    }
}

static void convert_scaled(const PcmSpan *span, int bits, const AudioEngine *engine)
{
    uint32_t *words = span->words;
    bool mono = span->left == span->right && engine->left_gain == engine->right_gain;
    for (uint32_t index = 0; index < span->frames; index++, words += WORDS_PER_FRAME) {
        words[0] =
            (uint32_t)headphone_audio_sample(span->left[index], (uint8_t)bits, engine->left_gain)
            << span->shift;
        words[1] = mono ? words[0]
                        : (uint32_t)headphone_audio_sample(span->right[index], (uint8_t)bits,
                                                           engine->right_gain)
                              << span->shift;
    }
}

void audio_convert_pending(const AudioEngine *engine, uint32_t *words, uint32_t frames, int shift)
{
    uint32_t at = engine->pending_offset;
    PcmSpan span = {words, engine->frame.left + at, engine->frame.right + at, frames, shift};
    int bits = engine->frame.bits;
    if (engine->left_gain != UNITY_GAIN || engine->right_gain != UNITY_GAIN ||
        (bits != SIXTEEN_BITS && bits != OUTPUT_BITS)) {
        convert_scaled(&span, bits, engine);
    } else if (bits == SIXTEEN_BITS) {
        convert_16(&span);
    } else {
        convert_24(&span);
    }
}

static bool same_curve(const HalAudioEqualizer *first, const HalAudioEqualizer *second)
{
    if (first->is_parametric != second->is_parametric ||
        (first->is_parametric && !parametric_same(&first->parametric, &second->parametric))) {
        return false;
    }
    bool same = first->band_count == second->band_count && first->pre_gain == second->pre_gain;
    for (int band = 0; same && band < first->band_count && band < HAL_AUDIO_EQUALIZER_BANDS;
         band++) {
        same = first->band_hz[band] == second->band_hz[band] &&
               first->gain_half_db[band] == second->gain_half_db[band];
    }
    return same;
}

void audio_engine_set_equalizer(AudioEngine *engine, const HalAudioEqualizer *curve)
{
    if (same_curve(&engine->equalizer_curve, curve)) {
        return;
    }
    HalAudioEqualizer before = engine->equalizer_curve;
    engine->equalizer_curve = *curve;
    if (engine->equalizing && engine->config.equalizer != NULL) {
        engine->equalizing = equalizer_retune(
            engine->config.equalizer,
            (EqualizerChange){.before = &before, .after = &engine->equalizer_curve},
            engine->output_rate);
    } else {
        engine->equalizing = engine->config.equalizer != NULL &&
                             equalizer_design(engine->config.equalizer, &engine->equalizer_curve,
                                              engine->output_rate);
    }
}
