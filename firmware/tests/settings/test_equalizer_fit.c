#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "settings/equalizer_fit.h"

static bool fit_text(const char *text, int8_t *gains)
{
    EqualizerFit fit;
    equalizer_fit_start(&fit);
    equalizer_fit_text(&fit, text, (uint32_t)strlen(text));
    return equalizer_fit_finish(&fit, gains);
}

static void check_gains(const int8_t *gains, const int8_t *expected)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(expected[band], gains[band]);
    }
}

static void band_lines_on_the_band_centres_are_taken_as_they_are(void)
{
    static const int8_t expected[SETTINGS_EQUALIZER_BANDS] = {3, -4, 0, 1, 2, -2, 5, 6, -7, 8};
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("31 1.5\n62 -2\n125 0\n250 0.5\n500 1\n1000 -1\n2000 2.5\n4000 3\n8000 -3.5\n"
                   "16000 4\n",
                   gains));
    check_gains(gains, expected);
}

static void gains_are_rounded_to_the_nearest_half_decibel_away_from_zero(void)
{
    static const int8_t expected[SETTINGS_EQUALIZER_BANDS] = {0, 1, 1, 2, -1, -1, -2, 3, 3, 0};
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("31 0.24\n62 0.25\n125 0.74\n250 0.75\n500 -0.25\n1000 -0.74\n2000 -0.75\n"
                   "4000 1.2999\n8000 1.5\n16000 0\n",
                   gains));
    check_gains(gains, expected);
}

static void gains_are_clamped_to_ten_decibels(void)
{
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("31 25\n62 -30\n125 10.2\n250 -10.2\n500 10\n", gains));
    CHECK_EQUAL(20, gains[0]);
    CHECK_EQUAL(-20, gains[1]);
    CHECK_EQUAL(20, gains[2]);
    CHECK_EQUAL(-20, gains[3]);
    CHECK_EQUAL(20, gains[4]);
}

static void a_graphic_eq_line_is_interpolated_between_its_points(void)
{
    static const int8_t expected[SETTINGS_EQUALIZER_BANDS] = {1, 2, 4, 5, 7, 8, 4, 1, -3, -7};
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("Preamp: -6.2 dB\nGraphicEQ: 20 0; 1000 4; 20000 -4\n", gains));
    check_gains(gains, expected);
}

static void the_old_five_bands_interpolate_by_octave_distance(void)
{
    static const int8_t expected[SETTINGS_EQUALIZER_BANDS] = {-20, -20, -16, -4, 3,
                                                              8,   3,   4,   16, 20};
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    EqualizerFit fit;
    equalizer_fit_start(&fit);
    equalizer_fit_point(&fit, 100, -1000);
    equalizer_fit_point(&fit, 315, 0);
    equalizer_fit_point(&fit, 1000, 400);
    equalizer_fit_point(&fit, 3150, 0);
    equalizer_fit_point(&fit, 10000, 1000);
    CHECK(equalizer_fit_finish(&fit, gains));
    check_gains(gains, expected);
}

static void a_single_point_sets_every_band(void)
{
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("1000, 3", gains));
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(6, gains[band]);
    }
}

static void points_may_come_in_any_order(void)
{
    int8_t sorted[SETTINGS_EQUALIZER_BANDS];
    int8_t shuffled[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("20 0\n1000 4\n20000 -4\n", sorted));
    CHECK(fit_text("20000 -4\n20 0\n1000 4\n", shuffled));
    check_gains(shuffled, sorted);
}

static void separators_line_endings_and_comments_are_tolerated(void)
{
    int8_t plain[SETTINGS_EQUALIZER_BANDS];
    int8_t messy[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text("125 2\n1000 -2\n8000 4\n", plain));
    CHECK(fit_text("# my curve, 31 Hz 9 dB\r\n\r\n  125,\t2\r\n1000;-2\r\n+8000   +4", messy));
    check_gains(messy, plain);
}

static void lines_without_a_pair_are_ignored(void)
{
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(!fit_text("Preamp: -6.2 dB\n", gains));
    CHECK(!fit_text("", gains));
    CHECK(!fit_text("no numbers at all\n", gains));
}

static void frequencies_outside_the_audio_range_are_ignored(void)
{
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(!fit_text("5 10\n99999 10\n-100 3\n", gains));
    CHECK(fit_text("5 10\n1000 2\n", gains));
    CHECK_EQUAL(4, gains[0]);
}

static void text_fed_in_pieces_gives_the_same_curve(void)
{
    static const char TEXT[] = "GraphicEQ: 20 -1.25; 100 2.5; 1000 -0.75; 10000 3.5\n";
    int8_t whole[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text(TEXT, whole));
    for (uint32_t split = 1; split < sizeof TEXT - 1; split++) {
        EqualizerFit fit;
        int8_t pieces[SETTINGS_EQUALIZER_BANDS];
        equalizer_fit_start(&fit);
        equalizer_fit_text(&fit, TEXT, split);
        equalizer_fit_text(&fit, TEXT + split, (uint32_t)sizeof TEXT - 1 - split);
        CHECK(equalizer_fit_finish(&fit, pieces));
        check_gains(pieces, whole);
    }
}

static void a_dense_autoeq_line_follows_a_slope(void)
{
    char text[2048] = "GraphicEQ: ";
    size_t length = strlen(text);
    for (int point = 0; point < 127; point++) {
        double hz = 20.0;
        for (int step = 0; step < point; step++) {
            hz *= 1.0564;
        }
        int rounded = (int)(hz + 0.5);
        int gain_tenths = (point - 63) * 3;
        length += (size_t)snprintf(text + length, sizeof text - length, "%d %d.%d; ", rounded,
                                   gain_tenths / 10,
                                   gain_tenths < 0 ? -gain_tenths % 10 : gain_tenths % 10);
    }
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    CHECK(fit_text(text, gains));
    CHECK(gains[0] < gains[5]);
    CHECK(gains[5] < gains[9]);
    CHECK(gains[5] >= 4 && gains[5] <= 6);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(band_lines_on_the_band_centres_are_taken_as_they_are),
        TEST_CASE(gains_are_rounded_to_the_nearest_half_decibel_away_from_zero),
        TEST_CASE(gains_are_clamped_to_ten_decibels),
        TEST_CASE(a_graphic_eq_line_is_interpolated_between_its_points),
        TEST_CASE(the_old_five_bands_interpolate_by_octave_distance),
        TEST_CASE(a_single_point_sets_every_band),
        TEST_CASE(points_may_come_in_any_order),
        TEST_CASE(separators_line_endings_and_comments_are_tolerated),
        TEST_CASE(lines_without_a_pair_are_ignored),
        TEST_CASE(frequencies_outside_the_audio_range_are_ignored),
        TEST_CASE(text_fed_in_pieces_gives_the_same_curve),
        TEST_CASE(a_dense_autoeq_line_follows_a_slope),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
