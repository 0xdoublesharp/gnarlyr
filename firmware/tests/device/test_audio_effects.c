#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/audio_effects.h"
#include "device/audio_engine.h"

enum {
    RATE = 44100,
    SAMPLE_MAX = 8388607,
    SHIFT = 8,
    BLOCK = 1000,
    SKIP_RING = 4 * RATE,
    ENGINE_RING = 8192,
    ENGINE_BLOCK = 256,
    CHUNK = 1024,
    MARKER = 100000,
    CAPTURE = 8 * RATE
};

static const double PI = 3.14159265358979323846;

typedef struct Reference {
    double direct;
    double direct_previous;
    double direct_pole;
    double cross;
    double cross_pole;
} Reference;

static Reference reference(int level, double rate)
{
    static const double CUTOFF[] = {650, 700, 700};
    static const double FEED[] = {9.5, 6.0, 4.5};
    double low_db = FEED[level - 1] * -5.0 / 6.0 - 3.0;
    double high_db = FEED[level - 1] / 6.0 - 3.0;
    double low_gain = pow(10, low_db / 20.0);
    double high_cut = 1.0 - pow(10, high_db / 20.0);
    double high_hz = CUTOFF[level - 1] * pow(2.0, (low_db - 20.0 * log10(high_cut)) / 12.0);
    double low = exp(-2.0 * PI * CUTOFF[level - 1] / rate);
    double high = exp(-2.0 * PI * high_hz / rate);
    double scale = 1.0 / (1.0 - high_cut + 2.0 * high_cut * high + low_gain);
    return (Reference){.direct = (1.0 - high_cut * (1.0 - high)) * scale,
                       .direct_previous = -high * scale,
                       .direct_pole = high,
                       .cross = low_gain * (1.0 - low) * scale,
                       .cross_pole = low};
}

typedef struct Complex {
    double re;
    double im;
} Complex;

static Complex divide(Complex top, Complex bottom)
{
    double norm = bottom.re * bottom.re + bottom.im * bottom.im;
    return (Complex){(top.re * bottom.re + top.im * bottom.im) / norm,
                     (top.im * bottom.re - top.re * bottom.im) / norm};
}

static Complex response(Reference filter, bool cross, double angle)
{
    Complex delay = {cos(angle), -sin(angle)};
    Complex bottom = {1.0 - (cross ? filter.cross_pole : filter.direct_pole) * delay.re,
                      -(cross ? filter.cross_pole : filter.direct_pole) * delay.im};
    Complex top = cross ? (Complex){filter.cross, 0}
                        : (Complex){filter.direct + filter.direct_previous * delay.re,
                                    filter.direct_previous * delay.im};
    return divide(top, bottom);
}

static uint32_t word(int32_t sample)
{
    return (uint32_t)sample << SHIFT;
}

static int32_t sample_of(uint32_t value)
{
    return (int32_t)value >> SHIFT;
}

typedef struct Projection {
    double sine[2];
    double cosine[2];
} Projection;

static Projection measure(AudioCrossfeed *crossfeed, double hz, uint32_t rate, double amplitude)
{
    uint32_t words[2 * BLOCK];
    Projection projection = {0};
    uint32_t settle = rate / 10;
    for (uint32_t done = 0; done < settle + rate; done += BLOCK) {
        for (uint32_t index = 0; index < BLOCK; index++) {
            double phase = 2.0 * PI * hz * (double)((done + index) % rate) / rate;
            words[2 * index] = word((int32_t)lround(amplitude * sin(phase)));
            words[2 * index + 1] = 0;
        }
        audio_crossfeed_run(crossfeed, words, BLOCK);
        for (uint32_t index = 0; index < BLOCK; index++) {
            if (done + index < settle || done + index >= settle + rate) {
                continue;
            }
            double phase = 2.0 * PI * hz * (double)((done + index) % rate) / rate;
            for (uint32_t channel = 0; channel < 2; channel++) {
                double value = sample_of(words[2 * index + channel]);
                projection.sine[channel] += value * sin(phase);
                projection.cosine[channel] += value * cos(phase);
            }
        }
    }
    return projection;
}

static double decibels(double value)
{
    return 20.0 * log10(value);
}

static void check_response(int level, uint32_t rate, double hz)
{
    AudioCrossfeed crossfeed;
    audio_crossfeed_design(&crossfeed, (uint8_t)level, rate);
    double amplitude = 4194304.0;
    Projection projection = measure(&crossfeed, hz, rate, amplitude);
    Reference filter = reference(level, rate);
    for (int channel = 0; channel < 2; channel++) {
        double re = projection.sine[channel] * 2.0 / rate / amplitude;
        double im = projection.cosine[channel] * 2.0 / rate / amplitude;
        Complex expected = response(filter, channel == 1, 2.0 * PI * hz / rate);
        double gain_error = decibels(hypot(re, im)) - decibels(hypot(expected.re, expected.im));
        double phase_error = (atan2(im, re) - atan2(expected.im, expected.re)) * 180.0 / PI;
        CHECK(fabs(gain_error) < 0.02);
        CHECK(fabs(phase_error) < 0.1);
    }
}

static void crossfeed_matches_the_reference_gain_and_phase(void)
{
    static const uint32_t RATES[] = {32000, 44100, 48000, 96000, 192000};
    static const double FREQUENCIES[] = {50, 300, 700, 2000, 6000, 12000};
    for (int level = 1; level < HAL_AUDIO_CROSSFEED_LEVELS; level++) {
        for (unsigned rate = 0; rate < sizeof RATES / sizeof *RATES; rate++) {
            for (unsigned hz = 0; hz < sizeof FREQUENCIES / sizeof *FREQUENCIES; hz++) {
                check_response(level, RATES[rate], FREQUENCIES[hz]);
            }
        }
    }
}

static void crossfeed_strengths_feed_more_of_the_other_channel(void)
{
    double previous = 0;
    for (int level = 1; level < HAL_AUDIO_CROSSFEED_LEVELS; level++) {
        Reference filter = reference(level, RATE);
        Complex low = response(filter, true, 2.0 * PI * 100 / RATE);
        Complex direct = response(filter, false, 2.0 * PI * 100 / RATE);
        double ratio = hypot(low.re, low.im) / hypot(direct.re, direct.im);
        CHECK(ratio > previous);
        previous = ratio;
    }
}

static void crossfeed_keeps_mono_content_at_its_documented_level(void)
{
    static const double LEVEL_DB[] = {-1.82, -2.28, -2.42};
    for (int level = 1; level < HAL_AUDIO_CROSSFEED_LEVELS; level++) {
        AudioCrossfeed crossfeed;
        audio_crossfeed_design(&crossfeed, (uint8_t)level, RATE);
        uint32_t words[2 * BLOCK];
        for (int round = 0; round < 20; round++) {
            for (int index = 0; index < 2 * BLOCK; index++) {
                words[index] = word(4000000);
            }
            audio_crossfeed_run(&crossfeed, words, BLOCK);
        }
        double gain = decibels(sample_of(words[2 * BLOCK - 2]) / 4000000.0);
        CHECK(fabs(gain - LEVEL_DB[level - 1]) < 0.01);
        CHECK_EQUAL(sample_of(words[2 * BLOCK - 2]), sample_of(words[2 * BLOCK - 1]));
    }
}

static void run_reference(Reference filter, const int32_t *input, double *state, double *output)
{
    for (int channel = 0; channel < 2; channel++) {
        double previous = state[6 + channel];
        state[channel] = filter.direct * input[channel] + filter.direct_previous * previous +
                         filter.direct_pole * state[channel];
        state[2 + channel] = filter.cross * input[channel] + filter.cross_pole * state[2 + channel];
        state[6 + channel] = input[channel];
    }
    output[0] = state[0] + state[3];
    output[1] = state[1] + state[2];
}

static void crossfeed_never_clips_full_scale_input(void)
{
    for (int level = 1; level < HAL_AUDIO_CROSSFEED_LEVELS; level++) {
        AudioCrossfeed crossfeed;
        audio_crossfeed_design(&crossfeed, (uint8_t)level, RATE);
        Reference filter = reference(level, RATE);
        double state[8] = {0};
        srand(7);
        int32_t peak = 0;
        double worst = 0;
        for (int index = 0; index < 4 * RATE; index++) {
            int32_t input[2] = {index % 2000 < 1999 ? -SAMPLE_MAX : SAMPLE_MAX, SAMPLE_MAX};
            if (index >= 2 * RATE) {
                input[0] = rand() & 1 ? SAMPLE_MAX : -SAMPLE_MAX;
                input[1] = rand() & 1 ? SAMPLE_MAX : -SAMPLE_MAX;
            }
            uint32_t words[2] = {word(input[0]), word(input[1])};
            audio_crossfeed_run(&crossfeed, words, 1);
            double expected[2];
            run_reference(filter, input, state, expected);
            for (int channel = 0; channel < 2; channel++) {
                int32_t value = sample_of(words[channel]);
                peak = abs(value) > peak ? abs(value) : peak;
                double error = fabs(value - expected[channel]);
                worst = error > worst ? error : worst;
            }
        }
        CHECK(peak <= SAMPLE_MAX);
        CHECK(peak > SAMPLE_MAX - 256);
        CHECK(worst < 4.0);
    }
}

static uint32_t skip_ring[2 * SKIP_RING];
static int32_t skip_input[SKIP_RING];

static int32_t tone_at(uint32_t index)
{
    return (index / 50) % 2 ? 2000000 : -2000000;
}

typedef struct Pause {
    uint32_t start;
    uint32_t length;
} Pause;

static uint32_t fill_input(uint32_t total, const Pause *pauses, int count, int32_t floor)
{
    for (uint32_t index = 0; index < total; index++) {
        skip_input[index] = tone_at(index);
        for (int pause = 0; pause < count; pause++) {
            if (index >= pauses[pause].start &&
                index < pauses[pause].start + pauses[pause].length) {
                skip_input[index] = (index % 2) ? floor : -floor;
            }
        }
    }
    return total;
}

static uint32_t run_skip(AudioSkip *skip, uint32_t total, uint32_t buffered)
{
    uint32_t produced = 0;
    for (uint32_t done = 0; done < total; done += BLOCK) {
        uint32_t frames = total - done < BLOCK ? total - done : BLOCK;
        for (uint32_t index = 0; index < frames; index++) {
            skip_ring[2 * (produced + index)] = word(skip_input[done + index]);
            skip_ring[2 * (produced + index) + 1] = word(-skip_input[done + index]);
        }
        AudioEffectsBlock block = {.ring = skip_ring,
                                   .ring_frames = SKIP_RING,
                                   .first = produced,
                                   .frames = frames,
                                   .buffered = buffered,
                                   .stereo = true,
                                   .may_skip = true};
        produced += audio_skip_run(skip, &block);
    }
    return produced;
}

static void silence_longer_than_the_kept_length_is_cut_to_it(void)
{
    AudioSkip skip;
    audio_skip_design(&skip, RATE);
    Pause pause = {RATE, 2 * RATE};
    uint32_t total = fill_input(4 * RATE, &pause, 1, 0);
    uint32_t produced = run_skip(&skip, total, SKIP_RING);
    CHECK_EQUAL(2 * RATE + RATE / 4, produced);
    CHECK_EQUAL(1, skip.marks);
    CHECK_EQUAL(RATE + RATE / 4, skip.mark[0].at);
    CHECK_EQUAL(2 * RATE - RATE / 4, skip.mark[0].frames);
    for (uint32_t index = 0; index < RATE; index++) {
        CHECK_EQUAL(skip_input[index], sample_of(skip_ring[2 * index]));
    }
    for (uint32_t index = RATE + RATE / 4 + skip.fade; index < produced; index++) {
        CHECK_EQUAL(skip_input[index + skip.mark[0].frames], sample_of(skip_ring[2 * index]));
        CHECK_EQUAL(-skip_input[index + skip.mark[0].frames], sample_of(skip_ring[2 * index + 1]));
    }
}

static void the_cut_fades_back_in_without_a_step(void)
{
    AudioSkip skip;
    audio_skip_design(&skip, RATE);
    Pause pause = {RATE, 2 * RATE};
    uint32_t total = fill_input(4 * RATE, &pause, 1, 30000);
    uint32_t produced = run_skip(&skip, total, SKIP_RING);
    uint32_t cut = RATE + RATE / 4;
    CHECK_EQUAL(0, sample_of(skip_ring[2 * (cut - 1)]));
    double largest = 0;
    for (uint32_t index = cut; index < cut + skip.fade; index++) {
        double gain =
            (double)sample_of(skip_ring[2 * index]) / skip_input[index + skip.mark[0].frames];
        double expected = (double)(index - cut + 1) * skip.step / AUDIO_SKIP_UNITY;
        expected = expected > 1 ? 1 : expected;
        CHECK(fabs(gain - expected) < 0.0001);
        largest = gain > largest ? gain : largest;
    }
    CHECK(largest > 0.99);
    for (uint32_t index = cut - skip.fade; index + 1 < cut; index++) {
        CHECK(abs(sample_of(skip_ring[2 * (index + 1)])) <= abs(sample_of(skip_ring[2 * index])));
    }
    CHECK(produced > cut + skip.fade);
}

static void pauses_shorter_than_the_kept_length_pass_unchanged(void)
{
    AudioSkip skip;
    audio_skip_design(&skip, RATE);
    Pause pauses[] = {{RATE, RATE / 5}, {2 * RATE, RATE / 5}};
    uint32_t total = fill_input(3 * RATE, pauses, 2, 30000);
    CHECK_EQUAL(total, run_skip(&skip, total, SKIP_RING));
    CHECK_EQUAL(0, skip.marks);
    for (uint32_t index = 0; index < total; index++) {
        CHECK_EQUAL(skip_input[index], sample_of(skip_ring[2 * index]));
    }
}

static void a_low_buffer_keeps_the_pause_length_and_silences_it(void)
{
    AudioSkip skip;
    audio_skip_design(&skip, RATE);
    Pause pause = {RATE, RATE};
    uint32_t total = fill_input(3 * RATE, &pause, 1, 30000);
    CHECK_EQUAL(total, run_skip(&skip, total, SKIP_RING / 2 - 1));
    CHECK_EQUAL(0, skip.marks);
    for (uint32_t index = RATE + RATE / 4; index < 2 * RATE; index++) {
        CHECK_EQUAL(0, sample_of(skip_ring[2 * index]));
    }
}

static void a_full_mark_log_keeps_further_pauses(void)
{
    AudioSkip skip;
    audio_skip_design(&skip, RATE);
    Pause pauses[AUDIO_EFFECTS_MARKS + 1];
    for (int index = 0; index <= AUDIO_EFFECTS_MARKS; index++) {
        pauses[index] = (Pause){(uint32_t)(index * RATE / 2 + RATE / 10), RATE / 3};
    }
    uint32_t total = fill_input(3 * RATE, pauses, AUDIO_EFFECTS_MARKS + 1, 0);
    uint32_t produced = run_skip(&skip, total, SKIP_RING);
    CHECK_EQUAL(AUDIO_EFFECTS_MARKS, skip.marks);
    uint32_t each = RATE / 3 - RATE / 4;
    CHECK_EQUAL(total - AUDIO_EFFECTS_MARKS * each, produced);
}

static void marks_settle_into_the_segment_that_held_them(void)
{
    AudioSkip skip;
    audio_skip_design(&skip, RATE);
    skip.marks = 3;
    skip.mark[0] = (AudioSkipMark){20000, 100};
    skip.mark[1] = (AudioSkipMark){50000, 7};
    skip.mark[2] = (AudioSkipMark){60000, 3};
    AudioSegment segments[2] = {{.start = 0}, {.start = 50000}};
    audio_skip_settle(&skip, segments, 2, 55000);
    CHECK_EQUAL(107, segments[0].skipped);
    CHECK_EQUAL(0, segments[1].skipped);
    CHECK_EQUAL(1, skip.marks);
    audio_skip_settle(&skip, segments, 2, 59999);
    CHECK_EQUAL(1, skip.marks);
    audio_skip_settle(&skip, segments, 2, 60000);
    CHECK_EQUAL(3, segments[1].skipped);
    CHECK_EQUAL(0, skip.marks);
}

static double ratio(uint32_t gain)
{
    return (double)gain / AUDIO_BOOST_UNITY;
}

static void boost_gain_follows_the_square_root_law_between_its_limits(void)
{
    CHECK_EQUAL(AUDIO_BOOST_UNITY, audio_boost_target(0));
    CHECK_EQUAL(AUDIO_BOOST_UNITY, audio_boost_target(AUDIO_BOOST_FLOOR));
    CHECK(fabs(ratio(audio_boost_target(2 * AUDIO_BOOST_FLOOR)) - 2.0) < 0.001);
    CHECK_EQUAL(AUDIO_BOOST_MAX_GAIN, audio_boost_target(1u << 16));
    CHECK_EQUAL(AUDIO_BOOST_MAX_GAIN, audio_boost_target(1u << 18));
    for (uint32_t level = 1u << 18; level < (1u << 23); level += 4099) {
        double expected = sqrt((double)(1u << AUDIO_BOOST_TARGET_BITS) / level);
        CHECK(fabs(ratio(audio_boost_target(level)) / expected - 1.0) < 0.0005);
    }
    CHECK(fabs(ratio(audio_boost_target(1u << 20)) - 2.0) < 0.0001);
    CHECK(fabs(ratio(audio_boost_target(1u << 22)) - 1.0) < 0.0001);
    CHECK(fabs(ratio(audio_boost_target(1u << 23)) - sqrt(0.5)) < 0.0001);
    uint32_t previous = AUDIO_BOOST_MAX_GAIN;
    for (uint32_t level = 1u << 17; level < (1u << 23); level += 997) {
        uint32_t gain = audio_boost_target(level);
        CHECK(gain <= previous);
        previous = gain;
    }
}

static int32_t boost_peak(AudioBoost *boost, double amplitude, double hz, uint32_t frames)
{
    uint32_t words[2 * BLOCK];
    int32_t peak = 0;
    for (uint32_t done = 0; done < frames; done += BLOCK) {
        for (uint32_t index = 0; index < BLOCK; index++) {
            int32_t value = (int32_t)lround(amplitude * sin(2.0 * PI * hz * (done + index) / RATE));
            words[2 * index] = word(value);
            words[2 * index + 1] = word(-value);
        }
        audio_boost_run(boost, words, BLOCK);
        peak = 0;
        for (int index = 0; index < 2 * BLOCK; index++) {
            int32_t value = abs(sample_of(words[index]));
            peak = value > peak ? value : peak;
        }
    }
    return peak;
}

static void boost_raises_quiet_speech_and_lowers_loud_peaks(void)
{
    AudioBoost boost;
    audio_boost_design(&boost, RATE);
    double quiet = 8388608.0 * pow(10, -30 / 20.0);
    int32_t raised = boost_peak(&boost, quiet, 300, RATE);
    CHECK(fabs(raised / quiet - sqrt(4194304.0 / quiet)) < 0.03);
    audio_boost_design(&boost, RATE);
    int32_t lowered = boost_peak(&boost, 8000000.0, 300, RATE);
    CHECK(fabs(lowered - sqrt(4194304.0 * 8000000.0)) < 0.01 * lowered);
}

static void boost_leaves_room_noise_alone(void)
{
    AudioBoost boost;
    audio_boost_design(&boost, RATE);
    CHECK_EQUAL(4000, boost_peak(&boost, 4000.0, 300, RATE));
}

static void boost_never_exceeds_the_ceiling(void)
{
    AudioBoost boost;
    audio_boost_design(&boost, RATE);
    boost_peak(&boost, 20000.0, 200, 2 * RATE);
    uint32_t words[2 * BLOCK];
    srand(3);
    int32_t peak = 0;
    for (int round = 0; round < 200; round++) {
        for (int index = 0; index < 2 * BLOCK; index++) {
            int32_t value = round % 3 == 0 ? (rand() % 30000) - 15000
                                           : (rand() & 1 ? SAMPLE_MAX : -SAMPLE_MAX - 1);
            words[index] = word(value);
        }
        audio_boost_run(&boost, words, BLOCK);
        for (int index = 0; index < 2 * BLOCK; index++) {
            int32_t value = abs(sample_of(words[index]));
            peak = value > peak ? value : peak;
        }
    }
    CHECK(peak <= AUDIO_BOOST_CEILING);
    CHECK(peak < 3 * 8388608 / 4);
}

static void effects_that_are_off_leave_the_ring_untouched(void)
{
    static uint32_t ring[2 * 4096];
    static uint32_t copy[2 * 4096];
    for (int index = 0; index < 2 * 4096; index++) {
        ring[index] = word((index * 7919) % 200000 - 100000);
        copy[index] = ring[index];
    }
    AudioEffects effects = {0};
    audio_effects_design(&effects, RATE);
    AudioEffectsBlock block = {ring, 4096, 3000, 2000, 4096, true, true};
    CHECK_EQUAL(2000, audio_effects_run(&effects, &block));
    effects.settings = (HalAudioEffects){.crossfeed = 2, .speech_boost = true};
    block.stereo = false;
    effects.settings.crossfeed = 2;
    effects.settings.speech_boost = false;
    CHECK_EQUAL(2000, audio_effects_run(&effects, &block));
    CHECK(memcmp(ring, copy, sizeof ring) == 0);
}

static void effects_process_across_the_ring_end(void)
{
    static uint32_t ring[2 * 4096];
    static uint32_t straight[2 * 2000];
    for (int index = 0; index < 2 * 2000; index++) {
        straight[index] = word((index * 7919) % 200000 - 100000);
    }
    for (int index = 0; index < 2000; index++) {
        ring[2 * ((3000 + index) % 4096)] = straight[2 * index];
        ring[2 * ((3000 + index) % 4096) + 1] = straight[2 * index + 1];
    }
    AudioEffects split = {.settings = {.crossfeed = 3, .speech_boost = true}};
    AudioEffects whole = split;
    audio_effects_design(&split, RATE);
    audio_effects_design(&whole, RATE);
    AudioEffectsBlock block = {ring, 4096, 3000, 2000, 4096, true, true};
    audio_effects_run(&split, &block);
    AudioEffectsBlock linear = {straight, 2000, 0, 2000, 2000, true, true};
    audio_effects_run(&whole, &linear);
    for (int index = 0; index < 2000; index++) {
        CHECK_EQUAL(straight[2 * index], ring[2 * ((3000 + index) % 4096)]);
        CHECK_EQUAL(straight[2 * index + 1], ring[2 * ((3000 + index) % 4096) + 1]);
    }
}

typedef struct SpokenSource {
    int32_t left[ENGINE_BLOCK];
    int32_t right[ENGINE_BLOCK];
    uint32_t rate;
    uint32_t total;
    uint32_t next;
    uint32_t pause_start;
    uint32_t pause_end;
} SpokenSource;

static SpokenSource spoken;
static uint32_t engine_ring[2 * ENGINE_RING];
static AudioEngine engine;
static int32_t captured[CAPTURE];
static uint32_t positions[CAPTURE / CHUNK + 1];
static uint32_t tokens[CAPTURE / CHUNK + 1];

static bool spoken_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    SpokenSource *source = context;
    source->rate = path[0] == 'l' ? 16000 : RATE;
    source->total = 6 * source->rate;
    source->pause_start = source->rate;
    source->pause_end = 3 * source->rate;
    uint32_t start = (uint32_t)((uint64_t)start_ms * source->rate / 1000);
    source->next = start - start % ENGINE_BLOCK;
    *track = (AudioTrackInfo){.stream = {.sample_rate = source->rate,
                                         .channels = 2,
                                         .bits_per_sample = 24,
                                         .max_block = ENGINE_BLOCK,
                                         .total_samples = source->total}};
    return true;
}

static AudioFrameResult spoken_next(void *context, AudioFrame *frame)
{
    SpokenSource *source = context;
    if (source->next >= source->total) {
        return AUDIO_FRAME_END;
    }
    uint32_t left = source->total - source->next;
    uint32_t count = left < ENGINE_BLOCK ? left : ENGINE_BLOCK;
    for (uint32_t index = 0; index < count; index++) {
        uint32_t at = source->next + index;
        bool pause = at >= source->pause_start && at < source->pause_end;
        source->left[index] = pause ? 0 : (int32_t)(MARKER + at);
        source->right[index] = -source->left[index];
    }
    *frame = (AudioFrame){.left = source->left,
                          .right = source->right,
                          .first_sample = source->next,
                          .samples = count,
                          .bits = 24};
    source->next += count;
    return AUDIO_FRAME_READY;
}

static void spoken_release(void *context)
{
    (void)context;
}

static void spoken_close(void *context)
{
    (void)context;
}

static void start_spoken(void)
{
    spoken = (SpokenSource){0};
    AudioSource source = {spoken_open, spoken_next, spoken_release, spoken_close, &spoken};
    audio_engine_init(&engine, (AudioEngineConfig){.source = source,
                                                   .ring = engine_ring,
                                                   .ring_words = 2 * ENGINE_RING,
                                                   .resample_render = audio_resample_render});
    audio_engine_set_volume(&engine, AUDIO_ENGINE_VOLUME_MAX);
    audio_engine_set_effects(&engine, (HalAudioEffects){.skip_silence = true});
}

static uint32_t capture_until_stopped(void)
{
    uint32_t consumed = 0;
    uint32_t frames = 0;
    for (int chunk = 0; frames + CHUNK <= CAPTURE; chunk++) {
        for (int round = 0; round < 64; round++) {
            audio_engine_pump(&engine, consumed);
        }
        AudioEngineStatus status = audio_engine_status(&engine);
        if (status.state == AUDIO_ENGINE_STOPPED) {
            break;
        }
        positions[chunk] = status.position_ms;
        tokens[chunk] = status.token;
        for (uint32_t index = 0; index < CHUNK; index++) {
            captured[frames++] = sample_of(engine_ring[2 * ((consumed + index) % ENGINE_RING)]);
        }
        consumed += CHUNK;
    }
    return frames;
}

static void check_positions(uint32_t frames, uint32_t base_ms)
{
    int checked = 0;
    for (uint32_t chunk = 0; chunk * CHUNK + 1 < frames; chunk++) {
        int32_t value = captured[chunk * CHUNK];
        bool steady = value >= MARKER && captured[chunk * CHUNK + 1] == value + 1;
        if (steady && (uint32_t)(value - MARKER) >= base_ms * (RATE / 1000)) {
            uint32_t source = (uint32_t)(value - MARKER);
            CHECK_EQUAL((uint64_t)source * 1000 / RATE, positions[chunk]);
            checked++;
        }
    }
    CHECK(checked > 100);
}

static void skipped_silence_keeps_positions_in_source_time(void)
{
    start_spoken();
    AudioEngineTrack track = {.path = "spoken", .token = 1};
    CHECK(audio_engine_play(&engine, &track, 0));
    uint32_t frames = capture_until_stopped();
    uint32_t skipped = 2 * RATE - RATE / 4;
    CHECK(frames >= 6 * RATE - skipped - CHUNK);
    CHECK(frames < 6 * RATE - skipped + CHUNK);
    check_positions(frames, 0);
    CHECK_EQUAL(0, audio_engine_stats(&engine).underruns);
}

static void a_chapter_boundary_inside_a_pause_keeps_both_parts_exact(void)
{
    start_spoken();
    AudioEngineTrack first = {.path = "spoken", .end_ms = 2000, .token = 1};
    AudioEngineTrack second = {.path = "spoken", .start_ms = 2000, .token = 2};
    CHECK(audio_engine_play(&engine, &first, 0));
    audio_engine_queue_next(&engine, &second);
    uint32_t frames = capture_until_stopped();
    check_positions(frames, 0);
    bool switched = false;
    for (uint32_t chunk = 0; chunk * CHUNK < frames; chunk++) {
        switched = switched || tokens[chunk] == 2;
        CHECK(!switched || tokens[chunk] == 2);
        if (tokens[chunk] == 1) {
            CHECK(positions[chunk] < 2000);
        } else {
            CHECK(positions[chunk] >= 2000);
        }
    }
    CHECK(switched);
}

static void a_seek_after_a_skip_lands_on_the_reported_position(void)
{
    start_spoken();
    AudioEngineTrack track = {.path = "spoken", .token = 1};
    CHECK(audio_engine_play(&engine, &track, 0));
    uint32_t consumed = 0;
    while (audio_engine_status(&engine).position_ms < 3500) {
        audio_engine_pump(&engine, consumed);
        consumed += 64;
    }
    uint32_t position = audio_engine_status(&engine).position_ms;
    CHECK(audio_engine_seek(&engine, position));
    CHECK_EQUAL(position, audio_engine_status(&engine).position_ms);
}

static void converted_speech_skips_at_the_output_rate(void)
{
    start_spoken();
    AudioEngineTrack track = {.path = "low", .token = 1};
    CHECK(audio_engine_play(&engine, &track, 0));
    uint32_t frames = capture_until_stopped();
    uint32_t output = 32000;
    uint32_t expected = 6 * output - (2 * output - output / 4);
    CHECK(frames + CHUNK >= expected);
    CHECK(frames <= expected + CHUNK);
    CHECK(positions[frames / CHUNK - 1] > 5900);
    CHECK_EQUAL(0, audio_engine_stats(&engine).underruns);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(crossfeed_matches_the_reference_gain_and_phase),
        TEST_CASE(crossfeed_strengths_feed_more_of_the_other_channel),
        TEST_CASE(crossfeed_keeps_mono_content_at_its_documented_level),
        TEST_CASE(crossfeed_never_clips_full_scale_input),
        TEST_CASE(silence_longer_than_the_kept_length_is_cut_to_it),
        TEST_CASE(the_cut_fades_back_in_without_a_step),
        TEST_CASE(pauses_shorter_than_the_kept_length_pass_unchanged),
        TEST_CASE(a_low_buffer_keeps_the_pause_length_and_silences_it),
        TEST_CASE(a_full_mark_log_keeps_further_pauses),
        TEST_CASE(marks_settle_into_the_segment_that_held_them),
        TEST_CASE(boost_gain_follows_the_square_root_law_between_its_limits),
        TEST_CASE(boost_raises_quiet_speech_and_lowers_loud_peaks),
        TEST_CASE(boost_leaves_room_noise_alone),
        TEST_CASE(boost_never_exceeds_the_ceiling),
        TEST_CASE(effects_that_are_off_leave_the_ring_untouched),
        TEST_CASE(effects_process_across_the_ring_end),
        TEST_CASE(skipped_silence_keeps_positions_in_source_time),
        TEST_CASE(a_chapter_boundary_inside_a_pause_keeps_both_parts_exact),
        TEST_CASE(a_seek_after_a_skip_lands_on_the_reported_position),
        TEST_CASE(converted_speech_skips_at_the_output_rate),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
