#include "device/audio_engine.h"

#include <stddef.h>

#include "core/seam.h"

enum {
    UNITY_GAIN = 65536,
    GAIN_SHIFT = 16,
    OUTPUT_BITS = 24,
    WORD_BITS = 32,
    WORD_SHIFT = WORD_BITS - OUTPUT_BITS,
    WORDS_PER_FRAME = 2,
    SIXTEEN_BITS = 16,
    STAGE_FRAMES = AUDIO_RESAMPLE_HISTORY - AUDIO_RESAMPLE_LEFT,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608,
    MILLISECONDS_PER_SECOND = 1000,
    STEP_GAIN_Q16 = 58409,
    COASTING_CONVERSION_RATE = 32000
};

static void copy_path(char *destination, const char *source)
{
    size_t index = 0;
    while (source[index] != '\0' && index + 1 < AUDIO_ENGINE_PATH_CAPACITY) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static CORE_COLD void set_track_gain(AudioEngine *engine, uint32_t gain)
{
    engine->track_gain = gain != 0 ? gain : UNITY_GAIN;
    engine->output_gain =
        (uint32_t)(((uint64_t)engine->gain * engine->track_gain + UNITY_GAIN / 2) >> GAIN_SHIFT);
    engine->left_gain = headphone_audio_gain(engine->headphones.left, engine->output_gain);
    engine->right_gain = headphone_audio_gain(engine->headphones.right, engine->output_gain);
}

CORE_COLD void audio_engine_set_headphones(AudioEngine *engine, HeadphoneAudio profile)
{
    engine->headphones = profile;
    set_track_gain(engine, engine->track_gain);
}

CORE_COLD void audio_engine_init(AudioEngine *engine, AudioEngineConfig config)
{
    *engine = (AudioEngine){.config = config,
                            .state = AUDIO_ENGINE_STOPPED,
                            .gain = UNITY_GAIN,
                            .track_gain = UNITY_GAIN,
                            .output_gain = UNITY_GAIN,
                            .headphones = headphone_audio_defaults(),
                            .left_gain = UNITY_GAIN,
                            .right_gain = UNITY_GAIN};
}

CORE_COLD void audio_engine_set_volume(AudioEngine *engine, uint8_t level)
{
    if (level == 0) {
        engine->gain = 0;
        set_track_gain(engine, engine->track_gain);
        return;
    }
    uint32_t gain = UNITY_GAIN;
    for (int step = level; step < AUDIO_ENGINE_VOLUME_MAX; step++) {
        gain = (uint32_t)(((uint64_t)gain * STEP_GAIN_Q16 + UNITY_GAIN / 2) >> GAIN_SHIFT);
    }
    engine->gain = gain;
    set_track_gain(engine, engine->track_gain);
}

static CORE_COLD void design_equalizer(AudioEngine *engine)
{
    audio_effects_design(&engine->effects, engine->output_rate);
    engine->equalizing =
        engine->config.equalizer != NULL &&
        equalizer_design(engine->config.equalizer, &engine->equalizer_curve, engine->output_rate);
}

static bool same_curve(const HalAudioEqualizer *first, const HalAudioEqualizer *second)
{
    bool same = first->band_count == second->band_count && first->pre_gain == second->pre_gain;
    for (int band = 0; same && band < first->band_count && band < HAL_AUDIO_EQUALIZER_BANDS;
         band++) {
        same = first->band_hz[band] == second->band_hz[band] &&
               first->gain_half_db[band] == second->gain_half_db[band];
    }
    return same;
}

CORE_COLD void audio_engine_set_effects(AudioEngine *engine, HalAudioEffects effects)
{
    engine->effects.settings = effects;
}

CORE_COLD void audio_engine_set_equalizer(AudioEngine *engine, const HalAudioEqualizer *curve)
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
        design_equalizer(engine);
    }
}

static void release_frame(AudioEngine *engine)
{
    if (engine->holding_frame) {
        engine->config.source.release(engine->config.source.context);
        engine->holding_frame = false;
    }
    engine->pending_frames = 0;
}

static void close_file(AudioEngine *engine)
{
    if (engine->decoding) {
        release_frame(engine);
        engine->config.source.close(engine->config.source.context);
        engine->decoding = false;
    }
}

static uint32_t frame_limit(uint32_t end_ms, uint32_t rate)
{
    return (uint32_t)((uint64_t)end_ms * rate / MILLISECONDS_PER_SECOND);
}

static CORE_COLD bool open_track(AudioEngine *engine, const char *path, uint32_t start_ms,
                                 uint32_t end_ms)
{
    AudioTrackInfo track;
    if (!engine->config.source.open(engine->config.source.context, path, start_ms, &track)) {
        engine->refusal = (AudioRefusal){.problem = AUDIO_REFUSAL_UNREADABLE};
        return false;
    }
    FlacInfo info = track.stream;
    engine->decoding = true;
    if (audio_resample_rate(info.sample_rate) == 0) {
        close_file(engine);
        engine->refusal =
            (AudioRefusal){.problem = AUDIO_REFUSAL_RATE, .sample_rate = info.sample_rate};
        return false;
    }
    if (info.sample_rate != audio_resample_rate(info.sample_rate) &&
        (engine->config.resample_render == NULL ||
         (engine->config.prepare_resample != NULL && !engine->config.prepare_resample()))) {
        close_file(engine);
        engine->refusal = (AudioRefusal){.problem = AUDIO_REFUSAL_UNREADABLE};
        return false;
    }
    engine->skip_frames =
        (uint32_t)((uint64_t)start_ms * info.sample_rate / MILLISECONDS_PER_SECOND);
    engine->decode_rate = info.sample_rate;
    engine->decode_end_ms = end_ms;
    engine->decode_limit = frame_limit(end_ms, info.sample_rate);
    engine->decode_track = track;
    engine->holding_frame = false;
    engine->pending_frames = 0;
    engine->pending_offset = 0;
    return true;
}

static AudioFadeRamp fade_ramp(const AudioEngine *engine, uint32_t fade_ms, uint32_t start)
{
    uint32_t length = frame_limit(fade_ms, engine->output_rate);
    return (AudioFadeRamp){.start = start,
                           .length = length,
                           .reciprocal = length == 0 ? 0 : UINT32_MAX / length,
                           .active = length > 0};
}

static CORE_COLD void push_segment(AudioEngine *engine, const char *path, uint32_t token,
                                   uint32_t start, uint32_t base_ms)
{
    engine->fade_out = (AudioFadeRamp){0};
    engine->fade_in = fade_ramp(engine, engine->decode_fade_ms, start);
    engine->decode_fade_ms = 0;
    if (engine->segment_count == AUDIO_ENGINE_SEGMENTS) {
        for (int index = 1; index < AUDIO_ENGINE_SEGMENTS; index++) {
            engine->segments[index - 1] = engine->segments[index];
        }
        engine->segment_count--;
    }
    AudioSegment *segment = &engine->segments[engine->segment_count++];
    segment->token = token;
    segment->start = start;
    segment->base_ms = base_ms;
    segment->end_ms = engine->decode_end_ms;
    segment->rate = audio_resample_rate(engine->decode_rate);
    segment->replay_gain = engine->track_gain;
    segment->skipped = 0;
    segment->track = engine->decode_track;
    copy_path(segment->path, path);
}

static void reset_positions(AudioEngine *engine)
{
    engine->produced = 0;
    engine->consumed = 0;
    engine->segment_count = 0;
    engine->ended = false;
    engine->end_at = 0;
    engine->rate_pending = false;
}

static CORE_COLD bool start_track(AudioEngine *engine, const AudioEngineTrack *track,
                                  uint32_t start_ms)
{
    close_file(engine);
    reset_positions(engine);
    engine->stats = (AudioEngineStats){0};
    engine->failed = false;
    engine->decode_fade_ms = 0;
    set_track_gain(engine, track->replay_gain);
    engine->decode_token = track->token;
    if (!open_track(engine, track->path, start_ms, track->end_ms)) {
        engine->state = AUDIO_ENGINE_STOPPED;
        engine->failed = true;
        engine->segments[0] = (AudioSegment){.token = track->token, .rate = 0};
        engine->segment_count = 1;
        return false;
    }
    audio_resample_start(&engine->resample, engine->decode_rate);
    engine->output_rate = audio_resample_rate(engine->decode_rate);
    design_equalizer(engine);
    push_segment(engine, track->path, track->token, 0, start_ms);
    engine->state = AUDIO_ENGINE_PLAYING;
    return true;
}

CORE_COLD bool audio_engine_play(AudioEngine *engine, const AudioEngineTrack *track,
                                 uint32_t offset_ms)
{
    return start_track(engine, track, track->start_ms + offset_ms);
}

bool audio_engine_queue_needs_restart(const AudioEngine *engine, const AudioEngineTrack *track)
{
    if (track == NULL && !engine->has_next && engine->segment_count <= 1 && !engine->rate_pending &&
        !(engine->resample.ended && engine->decoding)) {
        return false;
    }
    return engine->state != AUDIO_ENGINE_STOPPED &&
           (engine->segment_count > 1 || engine->rate_pending || engine->ended ||
            engine->resample.ended);
}

CORE_COLD void audio_engine_queue_next(AudioEngine *engine, const AudioEngineTrack *track)
{
    if (audio_engine_queue_needs_restart(engine, track) &&
        !audio_engine_seek(engine, audio_engine_status(engine).position_ms)) {
        return;
    }
    engine->has_next = track != NULL;
    if (track != NULL) {
        engine->next_token = track->token;
        engine->next_start_ms = track->start_ms;
        engine->next_end_ms = track->end_ms;
        engine->next_gain = track->replay_gain;
        copy_path(engine->next_path, track->path);
    }
    engine->next_fade_ms = track != NULL ? track->fade_ms : 0;
}

CORE_COLD bool audio_engine_seek(AudioEngine *engine, uint32_t position_ms)
{
    if (engine->segment_count == 0) {
        return false;
    }
    AudioSegment playing = engine->segments[0];
    AudioEngineState state = engine->state;
    AudioEngineTrack track = {.path = playing.path,
                              .end_ms = playing.end_ms,
                              .token = playing.token,
                              .replay_gain = playing.replay_gain};
    bool started = start_track(engine, &track, position_ms);
    if (started && state == AUDIO_ENGINE_PAUSED) {
        engine->state = AUDIO_ENGINE_PAUSED;
    }
    return started;
}

void audio_engine_pause(AudioEngine *engine)
{
    if (engine->state == AUDIO_ENGINE_PLAYING) {
        engine->state = AUDIO_ENGINE_PAUSED;
    }
}

void audio_engine_resume(AudioEngine *engine)
{
    if (engine->state == AUDIO_ENGINE_PAUSED) {
        engine->state = AUDIO_ENGINE_PLAYING;
    }
}

void audio_engine_stop(AudioEngine *engine)
{
    close_file(engine);
    engine->state = AUDIO_ENGINE_STOPPED;
    engine->ended = true;
    engine->end_at = engine->produced;
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

static void convert_pending(const AudioEngine *engine, uint32_t *words, uint32_t frames, int shift)
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

static uint32_t ring_frames(const AudioEngine *engine)
{
    return engine->config.ring_words / WORDS_PER_FRAME;
}

static uint32_t free_space(const AudioEngine *engine)
{
    return ring_frames(engine) - (engine->produced - engine->consumed);
}

static uint32_t ring_span(const AudioEngine *engine, uint32_t **words)
{
    uint32_t start = engine->produced % ring_frames(engine);
    uint32_t contiguous = ring_frames(engine) - start;
    uint32_t space = free_space(engine);
    *words = &engine->config.ring[WORDS_PER_FRAME * start];
    return contiguous < space ? contiguous : space;
}

static uint32_t smaller(uint32_t first, uint32_t second)
{
    return first < second ? first : second;
}

static uint32_t input_available(const AudioEngine *engine)
{
    if (engine->decode_limit == 0) {
        return engine->pending_frames;
    }
    uint32_t position = engine->frame.first_sample + engine->pending_offset;
    uint32_t remaining = position < engine->decode_limit ? engine->decode_limit - position : 0;
    return smaller(engine->pending_frames, remaining);
}

static void consume_input(AudioEngine *engine, uint32_t frames)
{
    engine->pending_offset += frames;
    engine->pending_frames -= frames;
}

static bool at_limit(const AudioEngine *engine)
{
    return engine->decode_limit != 0 && engine->pending_frames > 0 &&
           engine->frame.first_sample + engine->pending_offset >= engine->decode_limit;
}

static void equalize(AudioEngine *engine, uint32_t first)
{
    uint32_t frames = engine->produced - first;
    if (!engine->equalizing || frames == 0) {
        return;
    }
    uint32_t start = first % ring_frames(engine);
    uint32_t piece = ring_frames(engine) - start;
    piece = piece < frames ? piece : frames;
    equalizer_run(engine->config.equalizer, &engine->config.ring[WORDS_PER_FRAME * start], piece);
    if (piece < frames) {
        equalizer_run(engine->config.equalizer, engine->config.ring, frames - piece);
    }
}

static bool same_text(const char *left, const char *right)
{
    size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return left[index] == right[index];
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

static void shape_block(AudioEngine *engine, uint32_t first, bool tail)
{
    uint32_t frames = engine->produced - first;
    if (frames == 0) {
        return;
    }
    AudioFadeBlock block = {.ring = engine->config.ring,
                            .ring_frames = ring_frames(engine),
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
}

static void refine_block(AudioEngine *engine, uint32_t first)
{
    uint32_t start = engine->segments[engine->segment_count - 1].start;
    AudioEffectsBlock block = {.ring = engine->config.ring,
                               .ring_frames = ring_frames(engine),
                               .first = first,
                               .frames = engine->produced - first,
                               .buffered = first - engine->consumed,
                               .stereo = engine->decode_track.stream.channels > 1,
                               .may_skip = !engine->fade_in.active && !engine->fade_out.active &&
                                           (int32_t)(first - start) > 0};
    engine->produced = first + audio_effects_run(&engine->effects, &block);
}

static void process_block(AudioEngine *engine, uint32_t first, bool tail)
{
    shape_block(engine, first, tail);
    equalize(engine, first);
    refine_block(engine, first);
}

static void render_resampled(AudioEngine *engine)
{
    uint32_t *words = NULL;
    uint32_t room = 0;
    while (engine->config.resample_render != NULL && (room = ring_span(engine, &words)) > 0) {
        uint32_t made = engine->config.resample_render(&engine->resample, words, room);
        engine->produced += made;
        if (made < room) {
            return;
        }
    }
}

static void interpolate_pending(AudioEngine *engine)
{
    render_resampled(engine);
    while (free_space(engine) > 0) {
        uint32_t frames = smaller(input_available(engine), audio_resample_room(&engine->resample));
        if (frames == 0) {
            return;
        }
        int32_t stage[WORDS_PER_FRAME * STAGE_FRAMES];
        convert_pending(engine, (uint32_t *)stage, frames, 0);
        consume_input(engine, audio_resample_push(&engine->resample, stage, frames));
        render_resampled(engine);
    }
}

static void copy_until_limit(AudioEngine *engine)
{
    uint32_t first = engine->produced;
    if (engine->resample.factor > 1) {
        interpolate_pending(engine);
        process_block(engine, first, false);
        return;
    }
    uint32_t *words = NULL;
    uint32_t frames = 0;
    while ((frames = smaller(input_available(engine), ring_span(engine, &words))) > 0) {
        convert_pending(engine, words, frames, WORD_SHIFT);
        engine->produced += frames;
        consume_input(engine, frames);
    }
    process_block(engine, first, false);
}

static void fill_silence(AudioEngine *engine)
{
    uint32_t *words = NULL;
    uint32_t frames = 0;
    while ((frames = ring_span(engine, &words)) > 0) {
        for (uint32_t index = 0; index < WORDS_PER_FRAME * frames; index++) {
            words[index] = 0;
        }
        engine->produced += frames;
    }
}

static void finish_decoding(AudioEngine *engine);

static bool continues_in_stream(const AudioEngine *engine)
{
    return engine->has_next && engine->next_start_ms == engine->decode_end_ms &&
           same_text(engine->segments[engine->segment_count - 1].path, engine->next_path);
}

static bool end_segment(AudioEngine *engine)
{
    if (!continues_in_stream(engine)) {
        release_frame(engine);
        finish_decoding(engine);
        return false;
    }
    engine->has_next = false;
    engine->decode_fade_ms = engine->fade_out.active ? engine->next_fade_ms : 0;
    set_track_gain(engine, engine->next_gain);
    engine->decode_token = engine->next_token;
    engine->decode_end_ms = engine->next_end_ms;
    engine->decode_limit = frame_limit(engine->next_end_ms, engine->decode_rate);
    push_segment(engine, engine->next_path, engine->next_token,
                 engine->produced + audio_resample_pending(&engine->resample),
                 engine->next_start_ms);
    return true;
}

static void copy_pending(AudioEngine *engine)
{
    bool crossing = true;
    while (crossing) {
        copy_until_limit(engine);
        crossing = at_limit(engine) && end_segment(engine);
    }
    if (engine->pending_frames == 0) {
        release_frame(engine);
    }
}

static void end_output(AudioEngine *engine)
{
    engine->ended = true;
    engine->end_at = engine->produced;
}

static bool open_following(AudioEngine *engine)
{
    close_file(engine);
    if (!engine->has_next) {
        return false;
    }
    engine->has_next = false;
    if (!open_track(engine, engine->next_path, engine->next_start_ms, engine->next_end_ms)) {
        engine->failed = true;
        return false;
    }
    set_track_gain(engine, engine->next_gain);
    engine->decode_token = engine->next_token;
    engine->decode_fade_ms = engine->fade_out.active ? engine->next_fade_ms : 0;
    return true;
}

static bool prepare_tail(AudioEngine *engine)
{
    if (engine->resample.factor <= 1 || engine->config.prepare_resample == NULL ||
        engine->config.prepare_resample()) {
        return true;
    }
    close_file(engine);
    audio_resample_start(&engine->resample, 0);
    engine->failed = true;
    end_output(engine);
    return false;
}

static bool begin_drain(AudioEngine *engine)
{
    uint32_t previous_rate = engine->decode_rate;
    if (open_following(engine) && engine->decode_rate == previous_rate) {
        push_segment(engine, engine->next_path, engine->next_token,
                     engine->produced + audio_resample_pending(&engine->resample),
                     engine->next_start_ms);
        return false;
    }
    if (!prepare_tail(engine)) {
        return false;
    }
    audio_resample_finish(&engine->resample);
    return true;
}

static void finish_decoding(AudioEngine *engine)
{
    if (!engine->resample.ended && !begin_drain(engine)) {
        return;
    }
    if (engine->resample.factor > 1) {
        uint32_t first = engine->produced;
        render_resampled(engine);
        process_block(engine, first, true);
        if (audio_resample_pending(&engine->resample) != 0) {
            return;
        }
    }
    if (!engine->decoding) {
        end_output(engine);
        return;
    }
    audio_resample_start(&engine->resample, engine->decode_rate);
    if (audio_resample_rate(engine->decode_rate) != engine->output_rate) {
        engine->rate_pending = true;
        return;
    }
    push_segment(engine, engine->next_path, engine->next_token, engine->produced,
                 engine->next_start_ms);
}

static void decode_one(AudioEngine *engine)
{
    if (engine->resample.ended) {
        finish_decoding(engine);
        return;
    }
    AudioFrame frame;
    AudioFrameResult result = engine->config.source.next(engine->config.source.context, &frame);
    if (result == AUDIO_FRAME_WAIT) {
        return;
    }
    if (result == AUDIO_FRAME_END) {
        finish_decoding(engine);
        return;
    }
    if (result != AUDIO_FRAME_READY) {
        engine->failed = true;
        close_file(engine);
        engine->ended = true;
        engine->end_at = engine->produced;
        return;
    }
    uint32_t skip = 0;
    if (engine->skip_frames > frame.first_sample) {
        skip = engine->skip_frames - frame.first_sample;
        skip = skip < frame.samples ? skip : frame.samples;
    }
    engine->frame = frame;
    engine->holding_frame = true;
    engine->skip_frames = 0;
    engine->pending_offset = skip;
    engine->pending_frames = frame.samples - skip;
    engine->stats.frames++;
}

static void drop_finished_segments(AudioEngine *engine)
{
    while (engine->segment_count > 1 &&
           (int32_t)(engine->consumed - engine->segments[1].start) >= 0) {
        for (int index = 1; index < engine->segment_count; index++) {
            engine->segments[index - 1] = engine->segments[index];
        }
        engine->segment_count--;
    }
}

static bool unfinished_audio(const AudioEngine *engine)
{
    return engine->decoding || (engine->resample.ended && !engine->ended);
}

void audio_engine_observe(AudioEngine *engine, uint32_t consumed)
{
    engine->consumed = consumed;
    if ((int32_t)(engine->produced - engine->consumed) < 0) {
        if (unfinished_audio(engine)) {
            engine->stats.underruns++;
        }
        engine->produced = engine->consumed;
    }
    audio_effects_settle(&engine->effects, engine->segments, engine->segment_count, consumed);
    drop_finished_segments(engine);
    if (engine->ended && (int32_t)(engine->consumed - engine->end_at) >= 0) {
        engine->state = AUDIO_ENGINE_STOPPED;
    }
}

void audio_engine_pump(AudioEngine *engine, uint32_t consumed)
{
    audio_engine_observe(engine, consumed);
    if (engine->rate_pending) {
        return;
    }
    copy_pending(engine);
    if (engine->pending_frames == 0 && unfinished_audio(engine) && free_space(engine) > 0) {
        decode_one(engine);
        if (!engine->rate_pending) {
            copy_pending(engine);
        }
    }
    if (!engine->decoding) {
        fill_silence(engine);
    }
}

bool audio_engine_wants_rate(const AudioEngine *engine, uint32_t *rate)
{
    if (!engine->rate_pending || (int32_t)(engine->consumed - engine->produced) < 0) {
        return false;
    }
    *rate = audio_resample_rate(engine->decode_rate);
    return true;
}

void audio_engine_restart(AudioEngine *engine)
{
    if (!engine->rate_pending) {
        return;
    }
    reset_positions(engine);
    engine->output_rate = audio_resample_rate(engine->decode_rate);
    design_equalizer(engine);
    push_segment(engine, engine->next_path, engine->next_token, 0, engine->next_start_ms);
}

bool audio_engine_needs_full_clock(const AudioEngine *engine)
{
    return engine->resample.factor > 1 &&
           (engine->resample.stereo || engine->output_rate > COASTING_CONVERSION_RATE);
}

uint32_t audio_engine_rate(const AudioEngine *engine)
{
    return engine->output_rate;
}

uint32_t audio_engine_buffered(const AudioEngine *engine, uint32_t consumed)
{
    return engine->produced - consumed;
}

AudioEngineStats audio_engine_stats(const AudioEngine *engine)
{
    return engine->stats;
}

AudioEngineStatus audio_engine_status(const AudioEngine *engine)
{
    if (engine->segment_count == 0) {
        return (AudioEngineStatus){.state = engine->state, .failed = engine->failed};
    }
    const AudioSegment *segment = &engine->segments[0];
    uint32_t played = engine->consumed - segment->start + segment->skipped;
    uint32_t position =
        segment->rate == 0 ? segment->base_ms
                           : segment->base_ms + (uint32_t)((uint64_t)played *
                                                           MILLISECONDS_PER_SECOND / segment->rate);
    return (AudioEngineStatus){.state = engine->state,
                               .token = segment->token,
                               .position_ms = position,
                               .failed = engine->failed,
                               .track = segment->track};
}

const char *audio_engine_current_path(const AudioEngine *engine)
{
    return engine->segment_count == 0 ? NULL : engine->segments[0].path;
}

AudioRefusal audio_engine_refusal(const AudioEngine *engine)
{
    return engine->refusal;
}

CORE_COLD void audio_engine_set_replay_gain(AudioEngine *engine, uint32_t token, uint32_t gain)
{
    for (int index = 0; index < engine->segment_count; index++) {
        if (engine->segments[index].token == token) {
            engine->segments[index].replay_gain = gain;
        }
    }
    if (engine->decoding && engine->decode_token == token) {
        set_track_gain(engine, gain);
    }
    if (engine->has_next && engine->next_token == token) {
        engine->next_gain = gain;
    }
}
