#include "device/audio_engine.h"

#include <stddef.h>

#include "core/seam.h"

enum {
    UNITY_GAIN = 65536,
    GAIN_SHIFT = 16,
    OUTPUT_BITS = 24,
    WORD_BITS = 32,
    WORDS_PER_FRAME = 2,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608,
    MILLISECONDS_PER_SECOND = 1000,
    STEP_GAIN_Q16 = 58409
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
}

CORE_COLD void audio_engine_init(AudioEngine *engine, AudioEngineConfig config)
{
    *engine = (AudioEngine){.config = config,
                            .state = AUDIO_ENGINE_STOPPED,
                            .gain = UNITY_GAIN,
                            .track_gain = UNITY_GAIN,
                            .output_gain = UNITY_GAIN};
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
        (engine->config.resample_pull == NULL ||
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

static CORE_COLD void push_segment(AudioEngine *engine, const char *path, uint32_t token,
                                   uint32_t start, uint32_t base_ms)
{
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

static int64_t floor_shift(int64_t value, int shift)
{
    return value >= 0 ? value >> shift : -((-value - 1) >> shift) - 1;
}

static int32_t to_output(int32_t sample, int bits, uint32_t gain)
{
    int64_t value = (int64_t)sample * gain;
    int shift = GAIN_SHIFT + bits - OUTPUT_BITS;
    if (shift > 0) {
        value = floor_shift(value + (INT64_C(1) << (shift - 1)), shift);
    } else if (shift < 0) {
        int64_t factor = INT64_C(1) << -shift;
        if (value > SAMPLE_MAX / factor || value < SAMPLE_MIN / factor) {
            return value < 0 ? SAMPLE_MIN : SAMPLE_MAX;
        }
        value *= factor;
    }
    return value > SAMPLE_MAX ? SAMPLE_MAX : value < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)value;
}

static uint32_t ring_frames(const AudioEngine *engine)
{
    return engine->config.ring_words / WORDS_PER_FRAME;
}

static uint32_t free_space(const AudioEngine *engine)
{
    return ring_frames(engine) - (engine->produced - engine->consumed);
}

static uint32_t output_word(int32_t sample)
{
    return (uint32_t)sample << (WORD_BITS - OUTPUT_BITS);
}

static void write_frame(AudioEngine *engine, int32_t left, int32_t right)
{
    uint32_t *slot =
        &engine->config.ring[WORDS_PER_FRAME * (engine->produced % ring_frames(engine))];
    slot[0] = output_word(left);
    slot[1] = output_word(right);
    engine->produced++;
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

static void copy_resampled(AudioEngine *engine)
{
    int32_t left = 0;
    int32_t right = 0;
    while (free_space(engine) > 0 && engine->config.resample_pull != NULL &&
           engine->config.resample_pull(&engine->resample, &left, &right)) {
        write_frame(engine, left, right);
    }
}

static void interpolate_pending(AudioEngine *engine)
{
    copy_resampled(engine);
    while (engine->pending_frames > 0 && free_space(engine) > 0 && !at_limit(engine)) {
        uint32_t at = engine->pending_offset;
        int bits = engine->frame.bits;
        if (!audio_resample_push(&engine->resample,
                                 to_output(engine->frame.left[at], bits, engine->output_gain),
                                 to_output(engine->frame.right[at], bits, engine->output_gain))) {
            break;
        }
        engine->pending_offset++;
        engine->pending_frames--;
        copy_resampled(engine);
    }
}

static void copy_until_limit(AudioEngine *engine)
{
    uint32_t first = engine->produced;
    if (engine->resample.factor > 1) {
        interpolate_pending(engine);
        equalize(engine, first);
        return;
    }
    const int32_t *left = engine->frame.left;
    const int32_t *right = engine->frame.right;
    int bits = engine->frame.bits;
    while (engine->pending_frames > 0 && free_space(engine) > 0 && !at_limit(engine)) {
        uint32_t index = engine->pending_offset;
        write_frame(engine, to_output(left[index], bits, engine->output_gain),
                    to_output(right[index], bits, engine->output_gain));
        engine->pending_offset++;
        engine->pending_frames--;
    }
    equalize(engine, first);
}

static void finish_decoding(AudioEngine *engine);

static bool same_text(const char *left, const char *right)
{
    size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return left[index] == right[index];
}

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
        copy_resampled(engine);
        equalize(engine, first);
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
        while (free_space(engine) > 0) {
            write_frame(engine, 0, 0);
        }
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
    uint32_t played = engine->consumed - segment->start;
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
