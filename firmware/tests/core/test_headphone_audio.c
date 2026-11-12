#include <math.h>
#include <stddef.h>

#include "check.h"
#include "core/headphone_audio.h"

static void switching_to_less_trim_lowers_volume_before_the_gain_changes(void)
{
    HeadphoneAudio before = {.left = 32768, .right = 32768, .ceiling = 40};
    HeadphoneAudio after = headphone_audio_defaults();
    CHECK_EQUAL(33, headphone_audio_switch_volume(before, after, 40));
    CHECK_EQUAL(0, headphone_audio_switch_volume(before, after, 0));
    CHECK_EQUAL(40, headphone_audio_switch_volume(after, before, 60));
}

static void balance_and_ceiling_changes_cannot_raise_either_channel(void)
{
    for (int old_left = 0; old_left <= 65536; old_left += 8192) {
        for (int new_left = 0; new_left <= 65536; new_left += 8192) {
            HeadphoneAudio before = {(uint32_t)old_left, 65536, 37};
            HeadphoneAudio after = {(uint32_t)new_left, 32768, 60};
            uint8_t level = headphone_audio_switch_volume(before, after, 55);
            CHECK(level <= 37);
            if (level != 0) {
                double attenuation = pow(10.0, ((double)level - 37) / 20.0);
                CHECK(new_left * attenuation <= old_left);
                CHECK(after.right * attenuation <= before.right);
            }
        }
    }
    HeadphoneAudio muted = {0, 65536, 60};
    CHECK_EQUAL(0, headphone_audio_switch_volume(muted, headphone_audio_defaults(), 60));
}

static void a_gain_rise_lowers_the_audible_level_by_the_rise_within_the_ceiling(void)
{
    HeadphoneAudio open = headphone_audio_defaults();
    HeadphoneAudio capped = {.left = REPLAY_GAIN_UNITY, .right = REPLAY_GAIN_UNITY, .ceiling = 20};
    CHECK_EQUAL(24, headphone_audio_rise_volume(open, 30, 6));
    CHECK_EQUAL(54, headphone_audio_rise_volume(open, 60, 6));
    CHECK_EQUAL(14, headphone_audio_rise_volume(capped, 40, 6));
    CHECK_EQUAL(14, headphone_audio_rise_volume(capped, 20, 6));
    CHECK_EQUAL(0, headphone_audio_rise_volume(open, 6, 6));
    CHECK_EQUAL(0, headphone_audio_rise_volume(open, 2, 6));
    CHECK_EQUAL(0, headphone_audio_rise_volume(open, 0, 6));
    CHECK_EQUAL(30, headphone_audio_rise_volume(open, 30, 0));
}

static void clipping_never_occurs_before_combined_attenuation(void)
{
    CHECK_EQUAL(2 * REPLAY_GAIN_UNITY,
                headphone_audio_gain(REPLAY_GAIN_UNITY / 2, 4 * REPLAY_GAIN_UNITY));
    CHECK_EQUAL(0, headphone_audio_gain(0, UINT32_MAX));
    CHECK_EQUAL(UINT32_MAX, headphone_audio_gain(REPLAY_GAIN_UNITY, UINT32_MAX));
}

static int32_t low_bit_reference(int32_t sample, uint8_t bits, uint32_t gain)
{
    int64_t value = (int64_t)sample * gain;
    int64_t factor = INT64_C(1) << (8 - bits);
    if (value > INT64_C(8388607) / factor || value < -INT64_C(8388608) / factor) {
        return value < 0 ? -8388608 : 8388607;
    }
    return (int32_t)(value * factor);
}

static void low_bit_scaling_matches_wide_division_at_both_clipping_boundaries(void)
{
    static const int32_t samples[] = {INT32_MIN, -8388608, -131073, -65537, -65536,  -1,
                                      0,         1,        65535,   65536,  8388607, INT32_MAX};
    static const uint32_t gains[] = {0, 1, 65536, UINT32_MAX};
    for (uint8_t bits = 1; bits < 8; bits++) {
        for (size_t sample = 0; sample < sizeof samples / sizeof samples[0]; sample++) {
            for (size_t gain = 0; gain < sizeof gains / sizeof gains[0]; gain++) {
                CHECK_EQUAL(low_bit_reference(samples[sample], bits, gains[gain]),
                            headphone_audio_sample(samples[sample], bits, gains[gain]));
            }
        }
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(switching_to_less_trim_lowers_volume_before_the_gain_changes),
        TEST_CASE(balance_and_ceiling_changes_cannot_raise_either_channel),
        TEST_CASE(a_gain_rise_lowers_the_audible_level_by_the_rise_within_the_ceiling),
        TEST_CASE(clipping_never_occurs_before_combined_attenuation),
        TEST_CASE(low_bit_scaling_matches_wide_division_at_both_clipping_boundaries),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
