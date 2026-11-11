#include "core/seam.h"
#include "device/audio_math.h"
#include "device/equalizer.h"

enum {
    GAIN_STEPS = 2 * EQUALIZER_GAIN_MAX_HALF_DB + 1,
    LOWEST_RATE_PER_HZ = 4
};

static const int32_t AMPLITUDE[GAIN_STEPS] = {
    603809400,  621440953,  639587356,  658263644,  677485290,  697268218,  717628817,
    738583957,  760150998,  782347808,  805192776,  828704830,  852903448,  877808678,
    903441154,  929822112,  956973408,  984917536,  1013677647, 1043277569, 1073741824,
    1105095651, 1137365027, 1170576685, 1204758142, 1239937715, 1276144550, 1313408643,
    1351760868, 1391232998, 1431857735, 1473668736, 1516700640, 1560989098, 1606570803,
    1653483517, 1701766107, 1751458575, 1802602089, 1855239020, 1909412977};
static const int64_t SQRT_HALF = 759250125;
static const int64_t TWO_PI = INT64_C(6746518852);

typedef struct Peaking {
    int64_t b0;
    int64_t b1;
    int64_t b2;
    int64_t a0;
    int64_t a1;
    int64_t a2;
} Peaking;

static int64_t one(void)
{
    return INT64_C(1) << AUDIO_MATH_FRACTION_BITS;
}

static int64_t multiply(int64_t first, int64_t second)
{
    return first * second / one();
}

static Peaking peaking(uint32_t hz, int gain_half_db, uint32_t rate)
{
    int64_t angle = TWO_PI * hz / rate;
    int64_t alpha = multiply(fixed_sine(angle), SQRT_HALF);
    int64_t boost = multiply(alpha, AMPLITUDE[EQUALIZER_GAIN_MAX_HALF_DB + gain_half_db]);
    int64_t cut = multiply(alpha, AMPLITUDE[EQUALIZER_GAIN_MAX_HALF_DB - gain_half_db]);
    int64_t twice_cosine = 2 * fixed_cosine(angle);
    return (Peaking){.b0 = one() + boost,
                     .b1 = -twice_cosine,
                     .b2 = one() - boost,
                     .a0 = one() + cut,
                     .a1 = -twice_cosine,
                     .a2 = one() - cut};
}

static int32_t normalized(int64_t value, int64_t scale, int64_t a0)
{
    int64_t scaled =
        value * scale / HAL_AUDIO_UNITY_PRE_GAIN * (INT64_C(1) << EQUALIZER_COEFFICIENT_BITS);
    int64_t half = a0 / 2;
    return (int32_t)(scaled >= 0 ? (scaled + half) / a0 : -((-scaled + half) / a0));
}

static EqualizerSection section_of(Peaking filter, int64_t pre_gain)
{
    return (EqualizerSection){.b0 = normalized(filter.b0, pre_gain, filter.a0),
                              .b1 = normalized(filter.b1, pre_gain, filter.a0),
                              .b2 = normalized(filter.b2, pre_gain, filter.a0),
                              .a1 = normalized(-filter.a1, HAL_AUDIO_UNITY_PRE_GAIN, filter.a0),
                              .a2 = normalized(-filter.a2, HAL_AUDIO_UNITY_PRE_GAIN, filter.a0)};
}

static bool band_usable(const HalAudioEqualizer *curve, int band, uint32_t rate)
{
    int gain = curve->gain_half_db[band];
    uint32_t hz = curve->band_hz[band];
    return gain != 0 && gain >= -EQUALIZER_GAIN_MAX_HALF_DB && gain <= EQUALIZER_GAIN_MAX_HALF_DB &&
           hz > 0 && hz < rate / LOWEST_RATE_PER_HZ;
}

static void add_section(Equalizer *equalizer, const HalAudioEqualizer *curve, int band,
                        uint32_t rate)
{
    int64_t pre_gain = equalizer->section_count == 0 ? curve->pre_gain : HAL_AUDIO_UNITY_PRE_GAIN;
    Peaking filter = peaking(curve->band_hz[band], curve->gain_half_db[band], rate);
    equalizer->sections[equalizer->section_count++] = section_of(filter, pre_gain);
}

static void clear_history(Equalizer *equalizer)
{
    for (int channel = 0; channel < EQUALIZER_CHANNELS; channel++) {
        for (int section = 0; section < EQUALIZER_SECTIONS; section++) {
            equalizer->history[channel][section] = (EqualizerHistory){0};
        }
    }
}

static bool build_sections(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate)
{
    equalizer->section_count = 0;
    if (rate == 0 || rate > EQUALIZER_HIGHEST_RATE) {
        return false;
    }
    int bands = curve->band_count < EQUALIZER_SECTIONS ? curve->band_count : EQUALIZER_SECTIONS;
    for (int band = 0; band < bands; band++) {
        if (band_usable(curve, band, rate)) {
            add_section(equalizer, curve, band, rate);
        }
    }
    return equalizer->section_count > 0;
}

CORE_SEAM bool equalizer_design(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate)
{
    bool designed = build_sections(equalizer, curve, rate);
    clear_history(equalizer);
    return designed;
}

static int section_of_band(const HalAudioEqualizer *curve, int band, uint32_t rate)
{
    if (band >= EQUALIZER_SECTIONS || !band_usable(curve, band, rate)) {
        return -1;
    }
    int index = 0;
    for (int before = 0; before < band; before++) {
        index += band_usable(curve, before, rate) ? 1 : 0;
    }
    return index;
}

static void carry_band(Equalizer *equalizer, const Equalizer *old, int from, int to)
{
    for (int channel = 0; channel < EQUALIZER_CHANNELS; channel++) {
        equalizer->history[channel][to] = old->history[channel][from];
    }
}

static uint32_t first_band_hz(const HalAudioEqualizer *curve, uint32_t rate)
{
    for (int band = 0; band < curve->band_count && band < EQUALIZER_SECTIONS; band++) {
        if (band_usable(curve, band, rate)) {
            return curve->band_hz[band];
        }
    }
    return 0;
}

static void carry_history(Equalizer *equalizer, const Equalizer *old, EqualizerChange change,
                          uint32_t rate)
{
    for (int band = 0; band < change.after->band_count; band++) {
        int to = section_of_band(change.after, band, rate);
        for (int other = 0; to >= 0 && other < change.before->band_count; other++) {
            int from = section_of_band(change.before, other, rate);
            if (from >= 0 && change.before->band_hz[other] == change.after->band_hz[band]) {
                carry_band(equalizer, old, from, to);
            }
        }
    }
}

bool equalizer_retune(Equalizer *equalizer, EqualizerChange change, uint32_t rate)
{
    Equalizer old = *equalizer;
    bool designed = equalizer_design(equalizer, change.after, rate);
    if (first_band_hz(change.before, rate) == first_band_hz(change.after, rate)) {
        carry_history(equalizer, &old, change, rate);
    }
    return designed;
}
