#include <math.h>

#include "check.h"
#include "settings/equalizer_response.h"

static const double PI = 3.14159265358979323846;

static double band_gain(uint16_t band, int gain, uint16_t frequency)
{
    double ratio = tan(PI * frequency / 44100.0) / tan(PI * band / 44100.0);
    double squared = ratio * ratio;
    double flat = (1.0 - squared) * (1.0 - squared);
    double amplitude = pow(10.0, gain / 40.0);
    return 10.0 * log10((flat + 2.0 * amplitude * squared) / (flat + 2.0 / amplitude * squared));
}

static double response_error(const SettingsEqualizerCurve *curve)
{
    EqualizerResponse response;
    CHECK(equalizer_response_make(&response, curve));
    double error = 0.0;
    for (int point = 0; point < EQUALIZER_RESPONSE_POINTS; point++) {
        uint16_t hz = equalizer_response_frequency(point);
        double expected = 0.0;
        for (int band = 0; band < curve->band_count; band++) {
            if (curve->band_hz[band] < 44100 / 4) {
                expected += band_gain(curve->band_hz[band], curve->gain_half_db[band], hz);
            }
        }
        double difference = fabs(expected - response.gain_tenth_db[point] / 10.0);
        error = difference > error ? difference : error;
    }
    return error;
}

static void the_reference_grid_spans_the_audible_bands_in_order(void)
{
    CHECK_EQUAL(31, equalizer_response_frequency(0));
    CHECK_EQUAL(18780, equalizer_response_frequency(EQUALIZER_RESPONSE_POINTS - 1));
    for (int point = 1; point < EQUALIZER_RESPONSE_POINTS; point++) {
        CHECK(equalizer_response_frequency(point) > equalizer_response_frequency(point - 1));
    }
}

static void factory_curves_match_the_reference_peaking_filters(void)
{
    Settings settings = settings_defaults();
    for (int preset = EQUALIZER_OFF; preset < EQUALIZER_CUSTOM; preset++) {
        settings.equalizer = (EqualizerPreset)preset;
        SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
        CHECK(response_error(&curve) < 0.15);
    }
}

static void the_ten_band_curve_matches_the_same_filter_model(void)
{
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_CUSTOM;
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        settings.equalizer_gains[band] = (int8_t)(band % 2 == 0 ? 13 : -7);
    }
    SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
    CHECK(response_error(&curve) < 0.15);
}

static void preamp_is_converted_from_the_actual_linear_gain(void)
{
    SettingsEqualizerCurve curve = {.pre_gain = SETTINGS_UNITY_PRE_GAIN / 4};
    EqualizerResponse response;
    CHECK(equalizer_response_make(&response, &curve));
    CHECK_EQUAL(-120, response.preamp_tenth_db);
}

static void bands_the_reference_rate_cannot_run_do_not_appear_in_its_response(void)
{
    SettingsEqualizerCurve curve = {.band_hz = {16000},
                                    .gain_half_db = {20},
                                    .band_count = 1,
                                    .pre_gain = SETTINGS_UNITY_PRE_GAIN};
    EqualizerResponse response;
    CHECK(equalizer_response_make(&response, &curve));
    for (int point = 0; point < EQUALIZER_RESPONSE_POINTS; point++) {
        CHECK_EQUAL(0, response.gain_tenth_db[point]);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(bands_the_reference_rate_cannot_run_do_not_appear_in_its_response),
        TEST_CASE(the_reference_grid_spans_the_audible_bands_in_order),
        TEST_CASE(factory_curves_match_the_reference_peaking_filters),
        TEST_CASE(the_ten_band_curve_matches_the_same_filter_model),
        TEST_CASE(preamp_is_converted_from_the_actual_linear_gain),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
