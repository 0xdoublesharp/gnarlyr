#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "library/replay_gain.h"

enum {
    PEAK_UNITY = 16777216
};

static bool offer(ReplayGainTags *tags, ReplayGainField field, const char *text)
{
    return replay_gain_offer(tags, field, text, (uint32_t)strlen(text));
}

static void standard_keys_are_case_insensitive_and_unrelated_keys_are_ignored(void)
{
    static const char *const KEYS[] = {"REPLAYGAIN_TRACK_GAIN", "replaygain_album_gain",
                                       "ReplayGain_Track_Peak", "rEpLaYgAiN_aLbUm_PeAk"};
    for (uint32_t index = 0; index < 4; index++) {
        CHECK_EQUAL(index, replay_gain_field(KEYS[index], (uint32_t)strlen(KEYS[index])));
    }
    CHECK_EQUAL(REPLAY_GAIN_NO_FIELD, replay_gain_field("REPLAYGAIN_TRACK_GAIN_X", 22));
    CHECK_EQUAL(REPLAY_GAIN_NO_FIELD, replay_gain_field("R128_TRACK_GAIN", 15));
}

static void decimal_scientific_whitespace_and_units_are_parsed_exactly(void)
{
    ReplayGainTags tags = {0};
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_GAIN, " \t-6.015 dB\r\n"));
    CHECK(offer(&tags, REPLAY_GAIN_ALBUM_GAIN, "+2e-2DB"));
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, ".125"));
    CHECK(offer(&tags, REPLAY_GAIN_ALBUM_PEAK, "1.25e+0"));
    CHECK_EQUAL(-602, tags.gain[0]);
    CHECK_EQUAL(2, tags.gain[1]);
    CHECK_EQUAL(PEAK_UNITY / 8, tags.peak[0]);
    CHECK_EQUAL(PEAK_UNITY * 5 / 4, tags.peak[1]);
    CHECK_EQUAL(15, tags.present);
}

static void malformed_or_out_of_range_tags_preserve_existing_values(void)
{
    static const char *const GAINS[] = {"",     " ",   "NaN",    "Inf",   "1 dB junk",
                                        "--1",  "1e",  "1e9999", "96.01", "-96.01",
                                        "0x10", "1,5", "."};
    static const char *const PEAKS[] = {"-1", "256", "1e99", "1 dB", "NaN", "1.2.3"};
    ReplayGainTags tags = {0};
    for (uint32_t index = 0; index < sizeof GAINS / sizeof GAINS[0]; index++) {
        CHECK(!offer(&tags, REPLAY_GAIN_TRACK_GAIN, GAINS[index]));
        CHECK_EQUAL(0, tags.present);
    }
    for (uint32_t index = 0; index < sizeof PEAKS / sizeof PEAKS[0]; index++) {
        CHECK(!offer(&tags, REPLAY_GAIN_TRACK_PEAK, PEAKS[index]));
        CHECK_EQUAL(0, tags.present);
    }
    static const char NUL[] = {'1', '.', '0', 0, 'd', 'B'};
    CHECK(!replay_gain_offer(&tags, REPLAY_GAIN_TRACK_GAIN, NUL, sizeof NUL));
    CHECK(!offer(&tags, REPLAY_GAIN_NO_FIELD, "1"));
}

static void the_first_valid_value_wins_without_poisoning_missing_tags(void)
{
    ReplayGainTags tags = {0};
    CHECK(!offer(&tags, REPLAY_GAIN_TRACK_GAIN, "bad"));
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_GAIN, "0 dB"));
    ReplayGainTags before = tags;
    CHECK(!offer(&tags, REPLAY_GAIN_TRACK_GAIN, "6 dB"));
    CHECK(memcmp(&before, &tags, sizeof tags) == 0);
    ReplayGain gain = replay_gain_finish(&tags);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, gain.track);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, gain.track_safe);
    CHECK_EQUAL(0, gain.album);
    CHECK_EQUAL(0, gain.album_safe);
}

static void fixed_point_amplitudes_match_independent_exponentiation(void)
{
    for (int value = -9600; value <= 9600; value++) {
        double expected = pow(10.0, (double)value / 2000.0) * REPLAY_GAIN_UNITY;
        double actual = replay_gain_amplitude((int16_t)value);
        double tolerance = expected * 0.0001;
        tolerance = tolerance > 2 ? tolerance : 2;
        CHECK(actual >= 1 && fabs(actual - expected) <= tolerance);
        if (actual < 1 || fabs(actual - expected) > tolerance) {
            break;
        }
    }
    CHECK_EQUAL(REPLAY_GAIN_UNITY, replay_gain_amplitude(0));
    CHECK(replay_gain_amplitude(-9600) > 0);
    CHECK(replay_gain_amplitude(9600) < UINT32_MAX);
}

static void clipping_uses_the_matching_peak_and_falls_back_conservatively(void)
{
    ReplayGainTags tags = {0};
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_GAIN, "6 dB"));
    CHECK(offer(&tags, REPLAY_GAIN_ALBUM_GAIN, "12 dB"));
    ReplayGain missing = replay_gain_finish(&tags);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, missing.track_safe);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, missing.album_safe);
    CHECK(offer(&tags, REPLAY_GAIN_ALBUM_PEAK, "2"));
    ReplayGain fallback = replay_gain_finish(&tags);
    CHECK_EQUAL(REPLAY_GAIN_UNITY / 2, fallback.track_safe);
    CHECK_EQUAL(REPLAY_GAIN_UNITY / 2, fallback.album_safe);
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, "0.25"));
    ReplayGain matching = replay_gain_finish(&tags);
    CHECK_EQUAL(matching.track, matching.track_safe);
    CHECK_EQUAL(REPLAY_GAIN_UNITY / 2, matching.album_safe);
}

static void peak_rounding_never_allows_the_declared_peak_above_full_scale(void)
{
    static const char *const PEAKS[] = {"0",      "0.1",  "1.000000000000001", "1.234567890123456",
                                        "255.75", "1e-99"};
    for (uint32_t index = 0; index < sizeof PEAKS / sizeof PEAKS[0]; index++) {
        ReplayGainTags tags = {0};
        CHECK(offer(&tags, REPLAY_GAIN_TRACK_GAIN, "96 dB"));
        CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, PEAKS[index]));
        ReplayGain gain = replay_gain_finish(&tags);
        CHECK(gain.track_safe > 0 && gain.track_safe <= gain.track);
        CHECK((uint64_t)gain.track_safe * tags.peak[0] <= (UINT64_C(1) << 40));
        if (index == 2) {
            CHECK(tags.peak[0] > PEAK_UNITY);
        }
        if (index == 5) {
            CHECK_EQUAL(1, tags.peak[0]);
        }
    }
}

static void peaks_just_above_binary_boundaries_round_up_from_all_decimal_digits(void)
{
    ReplayGainTags tags = {0};
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, "1.000000059604644775390626"));
    CHECK_EQUAL(PEAK_UNITY + 2, tags.peak[0]);
    tags = (ReplayGainTags){0};
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, "1.000000059604644775390625"));
    CHECK_EQUAL(PEAK_UNITY + 1, tags.peak[0]);
    tags = (ReplayGainTags){0};
    CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, "255.999999940395355224609375"));
    CHECK_EQUAL(UINT32_MAX, tags.peak[0]);
}

static void decimal_peak_quantization_matches_exact_binary_boundaries(void)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < 2048; index++) {
        char text[REPLAY_GAIN_TEXT_CAPACITY];
        (void)snprintf(text, sizeof text, "%.24f", (double)value / PEAK_UNITY);
        ReplayGainTags tags = {0};
        CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, text));
        CHECK_EQUAL(value, tags.peak[0]);
        if (value != UINT32_MAX) {
            (void)strcat(text, "0000000001");
            tags = (ReplayGainTags){0};
            CHECK(offer(&tags, REPLAY_GAIN_TRACK_PEAK, text));
            CHECK_EQUAL(value + 1, tags.peak[0]);
        }
        value = value * UINT32_C(1664525) + UINT32_C(1013904223);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(standard_keys_are_case_insensitive_and_unrelated_keys_are_ignored),
        TEST_CASE(decimal_scientific_whitespace_and_units_are_parsed_exactly),
        TEST_CASE(malformed_or_out_of_range_tags_preserve_existing_values),
        TEST_CASE(the_first_valid_value_wins_without_poisoning_missing_tags),
        TEST_CASE(fixed_point_amplitudes_match_independent_exponentiation),
        TEST_CASE(clipping_uses_the_matching_peak_and_falls_back_conservatively),
        TEST_CASE(peak_rounding_never_allows_the_declared_peak_above_full_scale),
        TEST_CASE(peaks_just_above_binary_boundaries_round_up_from_all_decimal_digits),
        TEST_CASE(decimal_peak_quantization_matches_exact_binary_boundaries),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
