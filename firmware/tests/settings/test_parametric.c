#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings/parametric_import.h"

#include "check.h"
#include "core/parametric.h"

static const double PI = 3.14159265358979323846;

static void reference(double *result, const ParametricFilter *filter, uint32_t rate)
{
    double a = pow(10, filter->gain / 400.0);
    double w = 2 * PI * filter->hz / rate;
    double c = cos(w);
    double alpha = sin(w) / (2 * filter->q / 100.0);
    double b0 = 1 + alpha * a, b1 = -2 * c, b2 = 1 - alpha * a;
    double a0 = 1 + alpha / a, a1 = -2 * c, a2 = 1 - alpha / a;
    if (filter->type != PARAMETRIC_PEAK) {
        double sign = filter->type == PARAMETRIC_LOW_SHELF ? 1 : -1;
        double beta = 2 * sqrt(a) * alpha;
        b0 = a * (a + 1 - sign * (a - 1) * c + beta);
        b1 = 2 * sign * a * (a - 1 - sign * (a + 1) * c);
        b2 = a * (a + 1 - sign * (a - 1) * c - beta);
        a0 = a + 1 + sign * (a - 1) * c + beta;
        a1 = -2 * sign * (a - 1 + sign * (a + 1) * c);
        a2 = a + 1 + sign * (a - 1) * c - beta;
    }
    result[0] = b0 / a0;
    result[1] = b1 / a0;
    result[2] = b2 / a0;
    result[3] = -a1 / a0;
    result[4] = -a2 / a0;
}

static void every_type_matches_double_precision_at_all_output_rates(void)
{
    static const uint32_t rates[] = {32000, 44100, 48000, 88200, 96000};
    static const uint16_t frequencies[] = {20, 31, 105, 1000, 8000, 15000, 20000};
    static const uint16_t qs[] = {30, 70, 141, 1000};
    static const int16_t gains[] = {-120, -23, 1, 62, 120};
    for (int type = 0; type < 3; type++) {
        for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++) {
            for (size_t f = 0; f < sizeof frequencies / sizeof frequencies[0]; f++) {
                if (2u * frequencies[f] >= rates[r]) {
                    continue;
                }
                for (size_t q = 0; q < sizeof qs / sizeof qs[0]; q++) {
                    for (size_t g = 0; g < sizeof gains / sizeof gains[0]; g++) {
                        ParametricFilter filter = {frequencies[f], qs[q], gains[g], (uint8_t)type,
                                                   true};
                        ParametricSection section;
                        CHECK(parametric_design(&section, &filter, rates[r]));
                        double x1 = 0, x2 = 0, norm = 0;
                        double unit = (double)(1 << PARAMETRIC_BITS);
                        for (int n = 0; n < 1024; n++) {
                            double y = (n == 0   ? section.b0
                                        : n == 1 ? section.b1
                                        : n == 2 ? section.b2
                                                 : 0) /
                                           unit +
                                       section.a1 / unit * x1 + section.a2 / unit * x2;
                            norm += fabs(y);
                            x2 = x1;
                            x1 = y;
                        }
                        CHECK(parametric_bound(&section) / unit >= norm);
                        double expected[5];
                        reference(expected, &filter, rates[r]);
                        int32_t actual[] = {section.b0, section.b1, section.b2, section.a1,
                                            section.a2};
                        for (int coefficient = 0; coefficient < 5; coefficient++) {
                            double error =
                                fabs(expected[coefficient] -
                                     actual[coefficient] / (double)(1 << PARAMETRIC_BITS));
                            if (error >= 3e-7) {
                                fprintf(stderr,
                                        "type=%d rate=%u hz=%u q=%u gain=%d coefficient=%d "
                                        "error=%.9g\n",
                                        type, rates[r], frequencies[f], qs[q], gains[g],
                                        coefficient, error);
                            }
                            CHECK(error < 3e-7);
                        }
                    }
                }
            }
        }
    }
}

static void autoeq_filters_preserve_parameters_and_skip_off_entries(void)
{
    const char *text = "Preamp: -6.2 dB\nFilter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 0.70\n"
                       "Filter 2: OFF PK Fc 400 Hz Gain 9 dB Q 2\n"
                       "Filter 3: ON LSC Fc 100 Hz Gain 3.4 dB Q 0.71\n"
                       "Filter 4: ON HS Fc 9000 Hz Gain -1.1 dB\n";
    for (uint32_t chunk = 1; chunk <= strlen(text); chunk++) {
        ParametricImport parser;
        parametric_import_start(&parser);
        for (uint32_t at = 0; at < strlen(text); at += chunk) {
            uint32_t left = (uint32_t)strlen(text) - at;
            parametric_import_text(&parser, text + at, left < chunk ? left : chunk);
        }
        ParametricEq eq;
        CHECK(parametric_import_finish(&parser, &eq));
        CHECK_EQUAL(3, eq.count);
        CHECK_EQUAL(-62, eq.preamp);
        CHECK_EQUAL(105, eq.filters[0].hz);
        CHECK_EQUAL(-23, eq.filters[0].gain);
        CHECK_EQUAL(70, eq.filters[0].q);
        CHECK_EQUAL(PARAMETRIC_LOW_SHELF, eq.filters[1].type);
        CHECK_EQUAL(PARAMETRIC_HIGH_SHELF, eq.filters[2].type);
        CHECK_EQUAL(71, eq.filters[2].q);
    }
}

static void headroom_bounds_the_response_and_impulse_at_every_rate(void)
{
    static const uint32_t rates[] = {32000, 44100, 48000, 88200, 96000};
    for (int type = 0; type < 3; type++) {
        for (int gain = -120; gain <= 120; gain += 240) {
            for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++) {
                ParametricEq eq = {.count = 1,
                                   .filters = {{105, 70, (int16_t)gain, (uint8_t)type, true}}};
                ParametricSection k;
                CHECK(parametric_design(&k, &eq.filters[0], rates[r]));
                double unit = (double)(1 << PARAMETRIC_BITS);
                double h1 = 0, h2 = 0, norm = 0;
                for (int n = 0; n < 96000; n++) {
                    double h = (n == 0   ? k.b0
                                : n == 1 ? k.b1
                                : n == 2 ? k.b2
                                         : 0) /
                                   unit +
                               k.a1 / unit * h1 + k.a2 / unit * h2;
                    norm += fabs(h);
                    h2 = h1;
                    h1 = h;
                }
                CHECK(parametric_bound(&k) / unit >= norm);
                uint16_t pre = parametric_pre_gain(&eq, rates[r]);
                CHECK(pre > 0);
                CHECK(pre / 32768.0 * norm < 0.81);
                for (int hz = 20; hz < 15000; hz += 113) {
                    double power = parametric_power(&k, (uint32_t)hz, rates[r]) / (double)(1 << 20);
                    CHECK(pre / 32768.0 * sqrt(power) < 0.81);
                }
            }
        }
    }
}

static bool parse(const char *text, ParametricEq *eq, ParametricImport *parser)
{
    parametric_import_start(parser);
    parametric_import_text(parser, text, (uint32_t)strlen(text));
    return parametric_import_finish(parser, eq);
}

static void malformed_filters_never_publish_a_partial_curve(void)
{
    static const char *const bad[] = {
        "Filter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 0.29",
        "Filter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 10.01",
        "Filter 1: ON PK Fc 19 Hz Gain -2.3 dB Q 0.7",
        "Filter 1: ON PK Fc 20001 Hz Gain -2.3 dB Q 0.7",
        "Filter 1: ON PK Fc 105 Hz Gain 12.1 dB Q 0.7",
        "Filter 1: ON PK Fc 105 Hz Gain -2.3 dB",
        "Filter 1: ON PK Fc nan Hz Gain -2.3 dB Q 0.7",
        "Filter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 0.7garbage",
        "Filter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 0.7 extra",
        "Filter 1: ON XX Fc 105 Hz Gain -2.3 dB Q 0.7",
        "Preamp: -61 dB\nFilter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 0.7",
    };
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; index++) {
        ParametricImport parser;
        ParametricEq eq = {.preamp = -123};
        CHECK(!parse(bad[index], &eq, &parser));
        CHECK_EQUAL(-123, eq.preamp);
    }
    ParametricImport parser;
    ParametricEq eq;
    CHECK(parse("Filter 1: OFF PK Fc nonsense\n", &eq, &parser));
    CHECK_EQUAL(0, eq.count);
    CHECK(parse("filter 1: on hs fc 8000 hz gain +1.3 db\r\n", &eq, &parser));
    CHECK_EQUAL(13, eq.filters[0].gain);
}

static void the_ten_strongest_filters_are_kept_and_reported(void)
{
    char text[2048];
    size_t at = 0;
    for (int index = 1; index <= 12; index++) {
        at += (size_t)snprintf(text + at, sizeof text - at,
                               "Filter %d: ON PK Fc %d Hz Gain %d dB Q 0.7\n", index, 100 * index,
                               index % 2 == 0 ? -index : index);
    }
    ParametricImport parser;
    ParametricEq eq;
    CHECK(parse(text, &eq, &parser));
    CHECK_EQUAL(10, eq.count);
    CHECK_EQUAL(12, parser.total);
    for (int index = 0; index < eq.count; index++) {
        CHECK(abs(eq.filters[index].gain) >= 30);
    }
}

static void cascade_response_sums_filters_and_preamp_never_raises_the_safe_limit(void)
{
    ParametricEq eq = {
        .count = 2,
        .filters = {{1000, 70, 62, PARAMETRIC_PEAK, true}, {1000, 70, -23, PARAMETRIC_PEAK, true}}};
    CHECK(abs(parametric_response(&eq, 1000, 44100) - 39) <= 1);
    uint16_t safe = parametric_pre_gain(&eq, 44100);
    eq.preamp = 120;
    CHECK_EQUAL(safe, parametric_pre_gain(&eq, 44100));
    eq.preamp = -600;
    CHECK(parametric_pre_gain(&eq, 44100) < safe);
    eq = (ParametricEq){.preamp = -62};
    CHECK(abs((int)parametric_pre_gain(&eq, 44100) - (int)lround(pow(10, -6.2 / 20) * 32768)) <= 1);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_type_matches_double_precision_at_all_output_rates),
        TEST_CASE(autoeq_filters_preserve_parameters_and_skip_off_entries),
        TEST_CASE(headroom_bounds_the_response_and_impulse_at_every_rate),
        TEST_CASE(malformed_filters_never_publish_a_partial_curve),
        TEST_CASE(the_ten_strongest_filters_are_kept_and_reported),
        TEST_CASE(cascade_response_sums_filters_and_preamp_never_raises_the_safe_limit),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
