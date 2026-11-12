#include "device/audio_tempo.h"

#include <stddef.h>

#include "core/seam.h"

_Static_assert(sizeof(AudioTempo) <= 128, "tempo control must fit its fixed caller budget");

enum {
    LOWEST_RATE = 8000,
    HIGHEST_RATE = 48000,
    MINIMUM_PITCH = 16,
    UNITY_SPEED = 100
};

CORE_SEAM uint32_t audio_tempo_buffer_frames(uint32_t rate)
{
    return rate >= LOWEST_RATE && rate <= HIGHEST_RATE ? 2 * (rate / MINIMUM_PITCH) + 2 : 0;
}

static bool spans_valid(AudioTempoBuffer buffer, const AudioTempo *tempo)
{
    uintptr_t first = (uintptr_t)buffer.first;
    uintptr_t second = (uintptr_t)buffer.second;
    uintptr_t first_bytes = (uintptr_t)buffer.first_frames * AUDIO_TEMPO_FRAME_BYTES;
    uintptr_t second_bytes = (uintptr_t)buffer.second_frames * AUDIO_TEMPO_FRAME_BYTES;
    if ((first == 0 && first_bytes != 0) || (second == 0 && second_bytes != 0) ||
        first > UINTPTR_MAX - first_bytes || second > UINTPTR_MAX - second_bytes) {
        return false;
    }
    uintptr_t state = (uintptr_t)tempo;
    bool first_clear =
        first_bytes == 0 || first + first_bytes <= state || first >= state + sizeof *tempo;
    bool second_clear =
        second_bytes == 0 || second + second_bytes <= state || second >= state + sizeof *tempo;
    return first_clear && second_clear &&
           (first_bytes == 0 || second_bytes == 0 || first + first_bytes <= second ||
            second + second_bytes <= first);
}

CORE_SEAM bool audio_tempo_start(AudioTempo *tempo, AudioTempoConfig config)
{
    uint32_t frames = (uint32_t)config.buffer.first_frames + config.buffer.second_frames;
    uint32_t needed = audio_tempo_buffer_frames(config.rate);
    if (needed == 0 || frames < needed || frames > UINT16_MAX || config.speed < 50 ||
        config.speed > AUDIO_TEMPO_FASTEST || !spans_valid(config.buffer, tempo)) {
        return false;
    }
    *tempo = (AudioTempo){.config = config, .capacity = (uint16_t)frames};
    return true;
}

static uint8_t *frame_at(const AudioTempo *tempo, uint32_t index)
{
    const AudioTempoBuffer *buffer = &tempo->config.buffer;
    index %= tempo->capacity;
    return index < buffer->first_frames
               ? buffer->first + index * AUDIO_TEMPO_FRAME_BYTES
               : buffer->second + (index - buffer->first_frames) * AUDIO_TEMPO_FRAME_BYTES;
}

static void put_sample(uint8_t *bytes, int32_t sample)
{
    uint32_t value = (uint32_t)sample;
    for (int index = 0; index < AUDIO_TEMPO_CHANNEL_BYTES; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static int32_t get_sample(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16;
    return (int32_t)(value & 0x800000u ? value | 0xFF000000u : value);
}

CORE_SEAM bool audio_tempo_push(AudioTempo *tempo, int32_t left, int32_t right)
{
    if (tempo->finished || tempo->available == tempo->capacity || left < -8388608 ||
        left > 8388607 || right < -8388608 || right > 8388607) {
        return false;
    }
    uint8_t *frame = frame_at(tempo, tempo->write++);
    put_sample(frame, left);
    put_sample(frame + AUDIO_TEMPO_CHANNEL_BYTES, right);
    tempo->write = (uint16_t)(tempo->write % tempo->capacity);
    tempo->available++;
    tempo->input_frames++;
    uint16_t credit = (uint16_t)(tempo->credit_remainder + UNITY_SPEED);
    while (credit >= tempo->config.speed) {
        tempo->credit++;
        credit = (uint16_t)(credit - tempo->config.speed);
    }
    tempo->credit_remainder = credit;
    return true;
}

typedef struct TempoFrame {
    int32_t left;
    int32_t right;
} TempoFrame;

enum {
    MAXIMUM_PITCH = 400,
    ANALYSIS_RATE = 1000,
    JOB_NONE = 0,
    JOB_SKIP,
    JOB_INSERT,
    JOB_BLEND
};

static inline TempoFrame input_frame(const AudioTempo *tempo, uint32_t offset)
{
    const uint8_t *frame = frame_at(tempo, (uint32_t)tempo->read + offset);
    return (TempoFrame){get_sample(frame), get_sample(frame + AUDIO_TEMPO_CHANNEL_BYTES)};
}

static void consume(AudioTempo *tempo, uint16_t frames)
{
    tempo->read = (uint16_t)(((uint32_t)tempo->read + frames) % tempo->capacity);
    tempo->available = (uint16_t)(tempo->available - frames);
    tempo->analysis_frames =
        tempo->analysis_frames > frames ? (uint16_t)(tempo->analysis_frames - frames) : 0;
}

static uint32_t distance(int32_t first, int32_t second)
{
    int32_t difference = first / 256 - second / 256;
    return (uint32_t)(difference < 0 ? -difference : difference);
}

static uint32_t difference(const AudioTempo *tempo, uint32_t period, uint32_t step)
{
    uint32_t sum = 0;
    uint32_t phase = 0;
    for (uint32_t index = 0; index < period; index += step) {
        uint32_t at = index + (step > 1 ? phase : 0);
        at = at < period ? at : index;
        phase = (phase + 1) & 3u;
        TempoFrame first = input_frame(tempo, at);
        TempoFrame second = input_frame(tempo, at + period);
        sum += distance(first.left, second.left) + distance(first.right, second.right);
    }
    return sum;
}

typedef struct PitchSearch {
    uint16_t low;
    uint16_t high;
    uint16_t step;
    uint16_t samples;
} PitchSearch;

static uint16_t best_period(const AudioTempo *tempo, PitchSearch search)
{
    uint16_t best = search.low;
    uint32_t best_difference = UINT32_MAX;
    uint32_t best_count = 1;
    for (uint32_t period = search.low; period <= search.high; period += search.step) {
        uint32_t value = difference(tempo, period, search.samples);
        uint32_t count = (period + search.samples - 1) / search.samples;
        if ((uint64_t)value * best_count < (uint64_t)best_difference * count) {
            best = (uint16_t)period;
            best_difference = value;
            best_count = count;
        }
    }
    return best;
}

static uint32_t mean_difference(const AudioTempo *tempo, uint16_t period)
{
    return (uint32_t)((uint64_t)difference(tempo, period, 1) * 256 / period);
}

static int32_t fractional_period(const AudioTempo *tempo, uint16_t period, uint16_t high)
{
    if (period <= 1 || period >= high) {
        return 0;
    }
    uint32_t low = mean_difference(tempo, (uint16_t)(period - 1));
    uint32_t centre = mean_difference(tempo, period);
    uint32_t above = mean_difference(tempo, (uint16_t)(period + 1));
    uint32_t peak = low > above ? low : above;
    if (peak <= centre) {
        return 0;
    }
    int64_t offset = ((int64_t)low - above) * 32768 / (peak - centre);
    return (int32_t)(offset > 32768 ? 32768 : offset < -32768 ? -32768 : offset);
}

static uint16_t scheduled_period(AudioTempo *tempo, uint16_t high)
{
    uint16_t period = tempo->cached_period;
    tempo->period_error += tempo->period_fraction;
    if (tempo->period_error >= 32768 && period < high) {
        tempo->period_error -= 65536;
        period++;
    } else if (tempo->period_error < -32768) {
        tempo->period_error += 65536;
        period--;
    }
    return period;
}

static uint16_t pitch_period(AudioTempo *tempo)
{
    uint16_t low = (uint16_t)(tempo->config.rate / MAXIMUM_PITCH);
    uint16_t high = (uint16_t)(tempo->config.rate / MINIMUM_PITCH);
    uint16_t available = (uint16_t)(tempo->available / 2);
    high = high < available ? high : available;
    if (high < low) {
        return 0;
    }
    if (tempo->analysis_frames != 0 && tempo->cached_period != 0 && tempo->cached_period < high) {
        return scheduled_period(tempo, high);
    }
    uint16_t step = (uint16_t)(tempo->config.rate / ANALYSIS_RATE);
    step = step != 0 ? step : 1;
    uint16_t coarse = best_period(tempo, (PitchSearch){low, high, step, step});
    uint16_t from = coarse > low + step ? (uint16_t)(coarse - step) : low;
    uint16_t to = coarse + step < high ? (uint16_t)(coarse + step) : high;
    uint16_t period = best_period(tempo, (PitchSearch){from, to, 1, step});
    tempo->cached_period = period;
    tempo->period_fraction = fractional_period(tempo, period, high);
    tempo->analysis_frames = (uint16_t)(tempo->config.rate / 100);
    return scheduled_period(tempo, high);
}

static void copy_after(AudioTempo *tempo)
{
    uint32_t speed = tempo->config.speed;
    uint32_t divisor = speed > UNITY_SPEED ? speed - UNITY_SPEED : UNITY_SPEED - speed;
    uint32_t multiplier = speed > UNITY_SPEED ? 2 * UNITY_SPEED - speed : 2 * speed - UNITY_SPEED;
    uint32_t numerator = tempo->period * multiplier + tempo->remainder;
    tempo->copy_frames = numerator / divisor;
    tempo->remainder = numerator % divisor;
}

static void shorten_skip(AudioTempo *tempo)
{
    uint32_t divisor = tempo->config.speed - UNITY_SPEED;
    uint32_t numerator = tempo->period * UNITY_SPEED + tempo->remainder;
    tempo->length = (uint16_t)(numerator / divisor);
    tempo->remainder = numerator % divisor;
    tempo->copy_frames = 0;
}

static bool begin_job(AudioTempo *tempo)
{
    if (!tempo->finished && tempo->available < audio_tempo_buffer_frames(tempo->config.rate) - 2) {
        return false;
    }
    uint16_t period = pitch_period(tempo);
    if (period == 0) {
        return false;
    }
    tempo->period = period;
    tempo->length = period;
    tempo->job_index = 0;
    tempo->job = tempo->config.speed > UNITY_SPEED ? JOB_SKIP : JOB_INSERT;
    if (tempo->config.speed > 2 * UNITY_SPEED) {
        shorten_skip(tempo);
    } else {
        copy_after(tempo);
    }
    tempo->mix_step = (uint32_t)(((UINT64_C(1) << 32) + tempo->length / 2) / tempo->length);
    return true;
}

static int32_t mix_sample(int32_t first, int32_t second, uint32_t index, uint32_t step)
{
    if (index == 0) {
        return first;
    }
    uint32_t weight = index * step;
    int64_t value = (int64_t)first * (0u - weight) + (int64_t)second * weight;
    int64_t half = INT64_C(1) << 31;
    return (int32_t)(value >= 0 ? (value + half) >> 32 : -((-value + half) >> 32));
}

static TempoFrame blend(TempoFrame first, TempoFrame second, uint32_t index, uint32_t step)
{
    return (TempoFrame){mix_sample(first.left, second.left, index, step),
                        mix_sample(first.right, second.right, index, step)};
}

static TempoFrame job_frame(AudioTempo *tempo)
{
    uint16_t index = tempo->job_index++;
    TempoFrame first = input_frame(tempo, index);
    TempoFrame second = input_frame(tempo, (uint32_t)tempo->period + index);
    TempoFrame result = tempo->job == JOB_INSERT ? first
                        : tempo->job == JOB_SKIP ? blend(first, second, index, tempo->mix_step)
                                                 : blend(second, first, index, tempo->mix_step);
    if (tempo->job_index == tempo->length) {
        uint16_t consumed =
            tempo->job == JOB_SKIP ? (uint16_t)(tempo->period + tempo->length) : tempo->period;
        if (tempo->job != JOB_INSERT) {
            consume(tempo, consumed);
        }
        tempo->job = tempo->job == JOB_INSERT ? JOB_BLEND : JOB_NONE;
        tempo->job_index = 0;
    }
    return result;
}

static TempoFrame tail_frame(AudioTempo *tempo)
{
    uint32_t period = tempo->period != 0 ? tempo->period : 1;
    period = tempo->input_frames < period ? (uint32_t)tempo->input_frames : period;
    uint32_t at = tempo->write + tempo->capacity - period + tempo->job_index++ % period;
    const uint8_t *frame = frame_at(tempo, at);
    return (TempoFrame){get_sample(frame), get_sample(frame + AUDIO_TEMPO_CHANNEL_BYTES)};
}

static bool next_frame(AudioTempo *tempo, TempoFrame *frame)
{
    if (tempo->job != JOB_NONE) {
        *frame = job_frame(tempo);
        return true;
    }
    if (tempo->copy_frames == 0 && tempo->config.speed != UNITY_SPEED && begin_job(tempo)) {
        *frame = job_frame(tempo);
        return true;
    }
    if (tempo->available != 0 &&
        (tempo->config.speed == UNITY_SPEED || tempo->copy_frames != 0 || tempo->finished)) {
        *frame = input_frame(tempo, 0);
        consume(tempo, 1);
        tempo->copy_frames -= tempo->copy_frames != 0 ? 1u : 0u;
        return true;
    }
    if (tempo->finished && tempo->input_frames != 0) {
        *frame = tail_frame(tempo);
        return true;
    }
    return false;
}

CORE_SEAM bool audio_tempo_pull(AudioTempo *tempo, int32_t *left, int32_t *right)
{
    if (tempo->config.speed == 0) {
        return false;
    }
    uint64_t limit = tempo->finished ? tempo->limit : tempo->credit;
    TempoFrame frame;
    if (tempo->output_frames >= limit || !next_frame(tempo, &frame)) {
        return false;
    }
    *left = frame.left;
    *right = frame.right;
    tempo->output_frames++;
    return true;
}

CORE_SEAM void audio_tempo_finish(AudioTempo *tempo)
{
    tempo->finished = true;
    tempo->limit =
        tempo->credit +
        (2u * tempo->credit_remainder >= tempo->config.speed && tempo->config.speed != 0 ? 1u : 0u);
}

CORE_SEAM bool audio_tempo_done(const AudioTempo *tempo)
{
    return tempo->finished && tempo->output_frames == tempo->limit;
}
