#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "check.h"
#include "device/equalizer.h"
#include "settings/settings.h"
#include "settings/sound_shaping.h"

enum {
    RATE_COUNT = 3,
    MAX_FRAMES = 96000,
    FULL_SCALE = 8388607,
    TEST_AMPLITUDE = 1 << 20,
    SWEEP_POINTS = 24
};

static const double MARGIN_DB = 2.0;
static const uint32_t RATES[RATE_COUNT] = {44100, 48000, 96000};
static const double PI = 3.14159265358979323846;

static Equalizer equalizer;
static uint32_t words[2 * MAX_FRAMES];

typedef struct Rbj {
    double b[3];
    double a[3];
} Rbj;

static HalAudioEqualizer one_band(uint16_t hz, int8_t gain_db)
{
    return (HalAudioEqualizer){.band_hz = {hz},
                               .gain_half_db = {(int8_t)(2 * gain_db)},
                               .band_count = 1,
                               .pre_gain = HAL_AUDIO_UNITY_PRE_GAIN};
}

static Rbj rbj_peaking(double hz, double gain_db, double rate)
{
    double amplitude = pow(10.0, gain_db / 40.0);
    double angle = 2.0 * PI * hz / rate;
    double alpha = sin(angle) / (2.0 * sqrt(0.5));
    double a0 = 1.0 + alpha / amplitude;
    return (Rbj){.b = {(1.0 + alpha * amplitude) / a0, -2.0 * cos(angle) / a0,
                       (1.0 - alpha * amplitude) / a0},
                 .a = {1.0, -2.0 * cos(angle) / a0, (1.0 - alpha / amplitude) / a0}};
}

static double rbj_gain_db(Rbj filter, double hz, double rate)
{
    double angle = 2.0 * PI * hz / rate;
    double numerator_re = filter.b[0] + filter.b[1] * cos(angle) + filter.b[2] * cos(2 * angle);
    double numerator_im = -filter.b[1] * sin(angle) - filter.b[2] * sin(2 * angle);
    double denominator_re = 1.0 + filter.a[1] * cos(angle) + filter.a[2] * cos(2 * angle);
    double denominator_im = -filter.a[1] * sin(angle) - filter.a[2] * sin(2 * angle);
    double numerator = numerator_re * numerator_re + numerator_im * numerator_im;
    double denominator = denominator_re * denominator_re + denominator_im * denominator_im;
    return 10.0 * log10(numerator / denominator);
}

static uint32_t fill_sine(double hz, double amplitude, uint32_t rate, uint32_t frames)
{
    for (uint32_t frame = 0; frame < frames; frame++) {
        int32_t sample = (int32_t)lround(amplitude * sin(2.0 * PI * hz * frame / rate));
        words[2 * frame] = (uint32_t)sample << 8;
        words[2 * frame + 1] = (uint32_t)sample << 8;
    }
    return frames;
}

static int32_t sample_at(uint32_t frame, int channel)
{
    return (int32_t)words[2 * frame + (uint32_t)channel] >> 8;
}

static double measured_gain_db(double hz, uint32_t rate)
{
    uint32_t frames = fill_sine(hz, TEST_AMPLITUDE, rate, rate / 2);
    equalizer_run(&equalizer, words, frames);
    double in_phase = 0.0;
    double quadrature = 0.0;
    uint32_t settled = frames / 2;
    for (uint32_t frame = settled; frame < frames; frame++) {
        double angle = 2.0 * PI * hz * frame / rate;
        in_phase += sample_at(frame, 0) * sin(angle);
        quadrature += sample_at(frame, 0) * cos(angle);
    }
    double amplitude =
        2.0 * sqrt(in_phase * in_phase + quadrature * quadrature) / (frames - settled);
    return 20.0 * log10(amplitude / (double)TEST_AMPLITUDE);
}

static bool close_to(double expected, double actual, double tolerance)
{
    return fabs(expected - actual) <= tolerance;
}

static void a_flat_curve_leaves_the_equalizer_off(void)
{
    HalAudioEqualizer flat = one_band(1000, 0);
    CHECK(!equalizer_design(&equalizer, &flat, 44100));
    HalAudioEqualizer none = {.pre_gain = HAL_AUDIO_UNITY_PRE_GAIN};
    CHECK(!equalizer_design(&equalizer, &none, 48000));
}

static void rates_above_96_khz_are_left_alone(void)
{
    HalAudioEqualizer boost = one_band(1000, 6);
    CHECK(equalizer_design(&equalizer, &boost, 96000));
    CHECK(!equalizer_design(&equalizer, &boost, 176400));
    CHECK(!equalizer_design(&equalizer, &boost, 192000));
}

static bool coefficients_match(uint16_t hz, int8_t gain_half_db, uint32_t rate)
{
    HalAudioEqualizer curve = one_band(hz, 0);
    curve.gain_half_db[0] = gain_half_db;
    CHECK(equalizer_design(&equalizer, &curve, rate));
    Rbj expected = rbj_peaking(hz, gain_half_db / 2.0, rate);
    const EqualizerSection *section = &equalizer.sections[0];
    double scale = (double)(1 << EQUALIZER_COEFFICIENT_BITS);
    double tolerance = 1e-7;
    return close_to(expected.b[0], section->b0 / scale, tolerance) &&
           close_to(expected.b[1], section->b1 / scale, tolerance) &&
           close_to(expected.b[2], section->b2 / scale, tolerance) &&
           close_to(-expected.a[1], section->a1 / scale, tolerance) &&
           close_to(-expected.a[2], section->a2 / scale, tolerance);
}

static void coefficients_follow_the_peaking_formula_at_every_rate(void)
{
    static const uint16_t bands[] = {31, 62, 63, 100, 125, 250, 500, 1000, 2000, 3150, 4000, 8000};
    static const int8_t gains[] = {-20, -6, 2, 12, 20, -19, 1, 3, 13};
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        for (size_t band = 0; band < sizeof bands / sizeof bands[0]; band++) {
            for (size_t gain = 0; gain < sizeof gains / sizeof gains[0]; gain++) {
                CHECK(coefficients_match(bands[band], gains[gain], RATES[rate]));
            }
        }
    }
}

static void band_centres_get_their_gain_at_every_rate(void)
{
    static const uint16_t bands[] = {100, 1000, 10000};
    static const int8_t gains[] = {-10, 6};
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        for (size_t band = 0; band < sizeof bands / sizeof bands[0]; band++) {
            for (size_t gain = 0; gain < sizeof gains / sizeof gains[0]; gain++) {
                HalAudioEqualizer curve = one_band(bands[band], gains[gain]);
                CHECK(equalizer_design(&equalizer, &curve, RATES[rate]));
                CHECK(close_to(gains[gain], measured_gain_db(bands[band], RATES[rate]), 0.05));
            }
        }
    }
}

static void the_curve_around_a_band_matches_its_own_rate(void)
{
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        HalAudioEqualizer curve = one_band(1000, 10);
        CHECK(equalizer_design(&equalizer, &curve, RATES[rate]));
        Rbj own = rbj_peaking(1000, 10, RATES[rate]);
        Rbj stock = rbj_peaking(1000, 10, 44100);
        double measured = measured_gain_db(2000, RATES[rate]);
        CHECK(close_to(rbj_gain_db(own, 2000, RATES[rate]), measured, 0.02));
        if (RATES[rate] != 44100) {
            CHECK(
                !close_to(rbj_gain_db(stock, 2000 * 44100.0 / RATES[rate], 44100), measured, 0.1));
        }
    }
}

static bool stays_below_full_scale(const HalAudioEqualizer *curve, uint32_t rate, double hz)
{
    if (!equalizer_design(&equalizer, curve, rate)) {
        return true;
    }
    uint32_t frames = fill_sine(hz, FULL_SCALE, rate, rate / 5);
    equalizer_run(&equalizer, words, frames);
    int32_t peak = 0;
    for (uint32_t frame = frames / 2; frame < frames; frame++) {
        int32_t sample = sample_at(frame, 0);
        peak = sample > peak ? sample : -sample > peak ? -sample : peak;
    }
    return peak < FULL_SCALE;
}

static HalAudioEqualizer hal_curve(const Settings *settings)
{
    SettingsEqualizerCurve curve = settings_equalizer_curve(settings);
    HalAudioEqualizer result = {.band_count = curve.band_count, .pre_gain = curve.pre_gain};
    for (int band = 0; band < curve.band_count; band++) {
        result.band_hz[band] = curve.band_hz[band];
        result.gain_half_db[band] = curve.gain_half_db[band];
    }
    return result;
}

static void check_never_clips(const Settings *settings)
{
    HalAudioEqualizer curve = hal_curve(settings);
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        for (int point = 0; point < SWEEP_POINTS; point++) {
            double hz = 25.0 * pow(2.0, point * 9.5 / (SWEEP_POINTS - 1));
            CHECK(stays_below_full_scale(&curve, RATES[rate], hz));
        }
    }
}

static void presets_with_their_pre_attenuation_never_clip_a_full_scale_sine(void)
{
    Settings settings = settings_defaults();
    for (int preset = EQUALIZER_RETRO; preset < EQUALIZER_CUSTOM; preset++) {
        settings.equalizer = (EqualizerPreset)preset;
        check_never_clips(&settings);
    }
}

static void custom_curves_with_their_pre_attenuation_never_clip_a_full_scale_sine(void)
{
    static const int8_t curves[][SETTINGS_EQUALIZER_BANDS] = {
        {20, 20, 20, 20, 20, 20, 20, 20, 20, 20}, {20, -20, 20, -20, 20, -20, 20, -20, 20, -20},
        {0, 0, 0, 0, 0, 20, 0, 0, 0, 0},          {-20, 14, 14, 14, 14, 14, 14, 14, 14, -20},
        {20, 20, 20, 20, 20, 0, 0, 0, 0, 0},      {0, 0, 0, 0, 0, 3, 5, 1, -1, 19}};
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_CUSTOM;
    for (size_t curve = 0; curve < sizeof curves / sizeof curves[0]; curve++) {
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            settings.equalizer_gains[band] = curves[curve][band];
        }
        check_never_clips(&settings);
    }
}

static void ten_bands_become_sections_for_every_band_below_a_quarter_of_the_rate(void)
{
    static const int sections[RATE_COUNT] = {9, 9, 10};
    HalAudioEqualizer curve = {.band_count = SETTINGS_EQUALIZER_BANDS,
                               .pre_gain = HAL_AUDIO_UNITY_PRE_GAIN};
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        curve.band_hz[band] = SETTINGS_EQUALIZER_BAND_HZ[band];
        curve.gain_half_db[band] = 1;
    }
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        CHECK(equalizer_design(&equalizer, &curve, RATES[rate]));
        CHECK_EQUAL(sections[rate], equalizer.section_count);
    }
}

static void half_decibel_boosts_measure_half_decibels_at_the_band_centres(void)
{
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        HalAudioEqualizer curve = one_band(1000, 0);
        curve.gain_half_db[0] = 3;
        CHECK(equalizer_design(&equalizer, &curve, RATES[rate]));
        CHECK(close_to(1.5, measured_gain_db(1000, RATES[rate]), 0.05));
        curve.gain_half_db[0] = -1;
        CHECK(equalizer_design(&equalizer, &curve, RATES[rate]));
        CHECK(close_to(-0.5, measured_gain_db(1000, RATES[rate]), 0.05));
    }
}

static void the_lowest_band_is_exact_at_96_khz(void)
{
    HalAudioEqualizer curve = one_band(31, 10);
    CHECK(equalizer_design(&equalizer, &curve, 96000));
    CHECK(close_to(10.0, measured_gain_db(31, 96000), 0.1));
}

static void channels_are_filtered_apart(void)
{
    HalAudioEqualizer curve = one_band(1000, 10);
    CHECK(equalizer_design(&equalizer, &curve, 48000));
    uint32_t frames = fill_sine(1000, TEST_AMPLITUDE, 48000, 4800);
    for (uint32_t frame = 0; frame < frames; frame++) {
        words[2 * frame + 1] = 0;
    }
    equalizer_run(&equalizer, words, frames);
    bool right_silent = true;
    for (uint32_t frame = 0; frame < frames; frame++) {
        right_silent = right_silent && sample_at(frame, 1) == 0;
    }
    CHECK(right_silent);
}

static void a_redesign_starts_from_silence(void)
{
    HalAudioEqualizer curve = one_band(100, 10);
    CHECK(equalizer_design(&equalizer, &curve, 44100));
    equalizer_run(&equalizer, words, fill_sine(100, TEST_AMPLITUDE, 44100, 4410));
    CHECK(equalizer_design(&equalizer, &curve, 44100));
    for (uint32_t frame = 0; frame < 64; frame++) {
        words[2 * frame] = 0;
        words[2 * frame + 1] = 0;
    }
    equalizer_run(&equalizer, words, 64);
    CHECK_EQUAL(0, sample_at(63, 0));
}

static HalAudioEqualizer custom_curve(const int8_t *gains)
{
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_CUSTOM;
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        settings.equalizer_gains[band] = gains[band];
    }
    return hal_curve(&settings);
}

static void a_single_boost_is_attenuated_by_its_peak_plus_the_margin(void)
{
    static const int8_t gains[SETTINGS_EQUALIZER_BANDS] = {0, 6, 0, 0, 0, 0, 0, 0, 0, 0};
    static const double boosts[] = {62, 1000, 10000};
    HalAudioEqualizer curve = custom_curve(gains);
    for (int rate = 0; rate < RATE_COUNT; rate++) {
        for (size_t tone = 0; tone < sizeof boosts / sizeof boosts[0]; tone++) {
            CHECK(equalizer_design(&equalizer, &curve, RATES[rate]));
            double expected = (tone == 0 ? 0.0 : -3.0) - MARGIN_DB;
            CHECK(close_to(expected, measured_gain_db(boosts[tone], RATES[rate]), 0.2));
        }
    }
}

static bool square_stays_below_full_scale(const HalAudioEqualizer *curve, uint32_t rate, double hz)
{
    uint32_t frames = rate / 2;
    uint32_t half_period = (uint32_t)lround(rate / hz / 2.0);
    for (uint32_t frame = 0; frame < frames; frame++) {
        int32_t sample = (frame / half_period) % 2 == 0 ? FULL_SCALE : -FULL_SCALE;
        words[2 * frame] = (uint32_t)sample << 8;
        words[2 * frame + 1] = words[2 * frame];
    }
    CHECK(equalizer_design(&equalizer, curve, rate));
    equalizer_run(&equalizer, words, frames);
    for (uint32_t frame = 0; frame < frames; frame++) {
        int32_t sample = sample_at(frame, 0);
        if (sample >= FULL_SCALE || sample <= -FULL_SCALE) {
            return false;
        }
    }
    return true;
}

static void cuts_leave_headroom_for_full_scale_square_waves(void)
{
    static const int8_t curves[][SETTINGS_EQUALIZER_BANDS] = {
        {-20, -20, 0, 0, 0, 0, 0, 0, 0, 0},
        {-10, -10, 0, 0, 0, 0, 0, 0, 0, 0},
        {-20, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, -20, 0, 0, 0, 0, 0, 0, 0, 0},
        {-20, -20, -20, -20, -20, -20, -20, -20, -20, -20},
        {-20, 0, -20, 0, -20, 0, -20, 0, -20, 0}};
    static const double tones[] = {31, 40, 62, 80, 100, 150, 200, 315, 500, 1000, 2000, 8000};
    for (size_t curve = 0; curve < sizeof curves / sizeof curves[0]; curve++) {
        HalAudioEqualizer hal = custom_curve(curves[curve]);
        for (int rate = 0; rate < RATE_COUNT; rate++) {
            for (size_t tone = 0; tone < sizeof tones / sizeof tones[0]; tone++) {
                CHECK(square_stays_below_full_scale(&hal, RATES[rate], tones[tone]));
            }
        }
    }
}

static void a_full_scale_kick_through_cuts_does_not_clip(void)
{
    static const int8_t gains[SETTINGS_EQUALIZER_BANDS] = {-20, -20};
    HalAudioEqualizer curve = custom_curve(gains);
    CHECK(equalizer_design(&equalizer, &curve, 44100));
    for (uint32_t frame = 0; frame < 22050; frame++) {
        double decay = exp(-(double)frame / 44100.0 / 0.08);
        int32_t sample =
            (int32_t)lround((double)FULL_SCALE * decay * cos(2.0 * PI * 60.0 * frame / 44100));
        words[2 * frame] = (uint32_t)sample << 8;
        words[2 * frame + 1] = words[2 * frame];
    }
    equalizer_run(&equalizer, words, 22050);
    for (uint32_t frame = 0; frame < 22050; frame++) {
        CHECK(abs(sample_at(frame, 0)) < FULL_SCALE);
    }
}

static int32_t largest_step_after(const HalAudioEqualizer *before, const HalAudioEqualizer *after,
                                  bool keep_history)
{
    enum {
        FRAMES = 4800,
        SPLIT = 1212
    };
    CHECK(equalizer_design(&equalizer, before, 48000));
    uint32_t frames = fill_sine(1000, TEST_AMPLITUDE, 48000, FRAMES);
    equalizer_run(&equalizer, words, SPLIT);
    EqualizerChange change = {.before = before, .after = after};
    CHECK(keep_history ? equalizer_retune(&equalizer, change, 48000)
                       : equalizer_design(&equalizer, after, 48000));
    equalizer_run(&equalizer, words + 2 * SPLIT, frames - SPLIT);
    int32_t largest = 0;
    for (uint32_t frame = SPLIT - 1; frame < frames - 1; frame++) {
        int32_t step = abs(sample_at(frame + 1, 0) - sample_at(frame, 0));
        largest = step > largest ? step : largest;
    }
    return largest;
}

static void a_gain_change_keeps_the_filter_history_and_does_not_click(void)
{
    static const int8_t first[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 6, 8};
    static const int8_t second[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 6, 10};
    static const int8_t added[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 6, 10, 0, 0, 2};
    HalAudioEqualizer one = custom_curve(first);
    HalAudioEqualizer louder = custom_curve(second);
    HalAudioEqualizer more = custom_curve(added);
    int32_t natural = (int32_t)((double)TEST_AMPLITUDE * 2.0 * PI * 1000.0 / 48000.0 * 1.8);
    CHECK(largest_step_after(&one, &louder, true) < natural);
    CHECK(largest_step_after(&one, &more, true) < natural);
    CHECK(largest_step_after(&one, &louder, false) > natural);
}

static void equalizer_off_leaves_the_samples_alone(void)
{
    HalAudioEqualizer off = custom_curve((const int8_t[SETTINGS_EQUALIZER_BANDS]){0});
    CHECK_EQUAL(SETTINGS_UNITY_PRE_GAIN, off.pre_gain);
    CHECK_EQUAL(0, off.band_count);
    CHECK(!equalizer_design(&equalizer, &off, 44100));
}

static void parametric_boosts_cuts_shelves_and_transients_stay_below_the_clamp(void)
{
    static const uint32_t rates[] = {32000, 44100, 48000, 88200, 96000};
    static const ParametricEq curves[] = {
        {.count = 3,
         .filters = {{20, 1000, 120, PARAMETRIC_PEAK, true},
                     {105, 30, -120, PARAMETRIC_LOW_SHELF, true},
                     {8000, 1000, 120, PARAMETRIC_HIGH_SHELF, true}}},
        {.count = 3,
         .filters = {{105, 70, -120, PARAMETRIC_PEAK, true},
                     {105, 70, -120, PARAMETRIC_LOW_SHELF, true},
                     {1000, 70, -120, PARAMETRIC_HIGH_SHELF, true}}},
    };
    for (size_t rate = 0; rate < sizeof rates / sizeof rates[0]; rate++) {
        for (size_t curve = 0; curve < sizeof curves / sizeof curves[0]; curve++) {
            HalAudioEqualizer hal = {.is_parametric = true, .parametric = curves[curve]};
            CHECK(square_stays_below_full_scale(&hal, rates[rate], 105));
            CHECK(stays_below_full_scale(&hal, rates[rate], 20));
            CHECK(stays_below_full_scale(&hal, rates[rate], 8000));
            for (int prefix = 1; prefix <= 3; prefix++) {
                CHECK(equalizer_design(&equalizer, &hal, rates[rate]));
                equalizer.section_count = prefix;
                uint32_t random = 17;
                uint32_t frames = rates[rate] / 5;
                for (uint32_t frame = 0; frame < frames; frame++) {
                    random = random * 1664525u + 1013904223u;
                    int32_t sample = random >> 31 ? FULL_SCALE : -FULL_SCALE;
                    words[2 * frame] = (uint32_t)sample << 8;
                    words[2 * frame + 1] = words[2 * frame];
                }
                equalizer_run(&equalizer, words, frames);
                for (uint32_t frame = 0; frame < frames; frame++) {
                    CHECK(abs(sample_at(frame, 0)) < FULL_SCALE);
                }
            }
        }
    }
}

static void parametric_preamp_works_without_filters_and_nyquist_filters_are_inactive(void)
{
    HalAudioEqualizer hal = {.is_parametric = true, .parametric = {.preamp = -62}};
    CHECK(equalizer_design(&equalizer, &hal, 32000));
    CHECK(close_to(-6.2, measured_gain_db(1000, 32000), 0.01));
    hal.parametric =
        (ParametricEq){.count = 1, .filters = {{20000, 70, 120, PARAMETRIC_PEAK, true}}};
    CHECK(!equalizer_design(&equalizer, &hal, 32000));
    CHECK(equalizer_design(&equalizer, &hal, 96000));
    CHECK(!equalizer_design(&equalizer, &hal, 192000));
}

static void sound_shaping_extremes_do_not_clip_full_scale_music_or_transients(void)
{
    static const uint32_t rates[] = {32000, 44100, 48000, 88200, 96000};
    for (int mask = 0; mask < 128; mask += 127) {
        SoundShaping shape = {.active = true};
        for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
            shape.positions[slider] = (int8_t)((mask >> slider & 1) != 0 ? 5 : -5);
        }
        ParametricEq base = {0};
        HalAudioEqualizer hal = {.is_parametric = true};
        CHECK(sound_shaping_build(&hal.parametric, &base, &shape));
        for (unsigned rate = 0; rate < sizeof rates / sizeof rates[0]; rate++) {
            CHECK(square_stays_below_full_scale(&hal, rates[rate], 90));
            CHECK(stays_below_full_scale(&hal, rates[rate], 300));
            CHECK(stays_below_full_scale(&hal, rates[rate], 12000));
            CHECK(equalizer_design(&equalizer, &hal, rates[rate]));
            for (uint32_t frame = 0; frame < 4096; frame++) {
                int32_t sample = frame == 0 || frame == 2048 ? FULL_SCALE : -FULL_SCALE;
                words[2 * frame] = (uint32_t)sample << 8;
                words[2 * frame + 1] = words[2 * frame];
            }
            equalizer_run(&equalizer, words, 4096);
            for (uint32_t frame = 0; frame < 4096; frame++) {
                CHECK(abs(sample_at(frame, 0)) < FULL_SCALE);
                CHECK(abs(sample_at(frame, 1)) < FULL_SCALE);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_flat_curve_leaves_the_equalizer_off),
        TEST_CASE(rates_above_96_khz_are_left_alone),
        TEST_CASE(ten_bands_become_sections_for_every_band_below_a_quarter_of_the_rate),
        TEST_CASE(half_decibel_boosts_measure_half_decibels_at_the_band_centres),
        TEST_CASE(the_lowest_band_is_exact_at_96_khz),
        TEST_CASE(coefficients_follow_the_peaking_formula_at_every_rate),
        TEST_CASE(band_centres_get_their_gain_at_every_rate),
        TEST_CASE(the_curve_around_a_band_matches_its_own_rate),
        TEST_CASE(presets_with_their_pre_attenuation_never_clip_a_full_scale_sine),
        TEST_CASE(custom_curves_with_their_pre_attenuation_never_clip_a_full_scale_sine),
        TEST_CASE(channels_are_filtered_apart),
        TEST_CASE(a_redesign_starts_from_silence),
        TEST_CASE(a_single_boost_is_attenuated_by_its_peak_plus_the_margin),
        TEST_CASE(cuts_leave_headroom_for_full_scale_square_waves),
        TEST_CASE(a_full_scale_kick_through_cuts_does_not_clip),
        TEST_CASE(a_gain_change_keeps_the_filter_history_and_does_not_click),
        TEST_CASE(equalizer_off_leaves_the_samples_alone),
        TEST_CASE(sound_shaping_extremes_do_not_clip_full_scale_music_or_transients),
        TEST_CASE(parametric_boosts_cuts_shelves_and_transients_stay_below_the_clamp),
        TEST_CASE(parametric_preamp_works_without_filters_and_nyquist_filters_are_inactive),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
