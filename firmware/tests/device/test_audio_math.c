#include "check.h"
#include "device/audio_math.h"

static void the_i2c_divider_matches_the_boot_clock_value(void)
{
    CHECK_EQUAL(0x000E000Eu, i2c_clock_divider(24000000, 100));
}

static void the_i2c_divider_follows_pclk(void)
{
    CHECK_EQUAL(0x001D001Du, i2c_clock_divider(48000000, 100));
    CHECK_EQUAL(0x001F001Eu, i2c_clock_divider(50000000, 100));
}

static void the_i2s_divider_truncates_or_rounds(void)
{
    CHECK_EQUAL(2, i2s_divider_truncated(11288960, 44100));
    CHECK_EQUAL(3, i2s_divider_rounded(11288960, 44100));
    CHECK_EQUAL(3, i2s_divider_truncated(12288000, 48000));
    CHECK_EQUAL(3, i2s_divider_rounded(12288000, 48000));
}

static void the_i2s_clock_register_holds_the_divider(void)
{
    CHECK_EQUAL(0x00023F3Fu, i2s_clock_register(2));
    CHECK_EQUAL(0x00033F3Fu, i2s_clock_register(3));
}

static void the_32_khz_clock_uses_an_exact_divider_and_the_documented_dac_rate(void)
{
    const AudioRate *rate = audio_clock_rate(32000);
    CHECK(rate != NULL);
    CHECK_EQUAL(12288000, rate->mclk);
    CHECK_EQUAL(0, rate->dac_byte);
    uint32_t divider = i2s_divider_rounded(rate->mclk, rate->rate);
    CHECK_EQUAL(5, divider);
    CHECK_EQUAL(32000, rate->mclk / (64 * (divider + 1)));
    CHECK_EQUAL(0x00053F3F, i2s_clock_register(divider));
    CHECK(audio_clock_rate(22050) == NULL);
}

static void one_sine_period_starts_at_zero_and_peaks_at_a_quarter(void)
{
    int16_t samples[100];
    tone_fill(samples, 100, 3000);
    CHECK_EQUAL(0, samples[0]);
    CHECK(samples[25] >= 2990 && samples[25] <= 3000);
    CHECK(samples[50] >= -10 && samples[50] <= 10);
    CHECK(samples[75] <= -2990 && samples[75] >= -3000);
}

static void the_sine_rises_then_falls(void)
{
    int16_t samples[100];
    tone_fill(samples, 100, 3000);
    for (int index = 1; index <= 25; index++) {
        CHECK(samples[index] > samples[index - 1]);
    }
    for (int index = 26; index <= 75; index++) {
        CHECK(samples[index] < samples[index - 1]);
    }
}

static void usb_rates_have_documented_dac_codes_and_bounded_clock_error(void)
{
    static const uint32_t rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
    static const uint32_t divisors[] = {4, 4, 2, 4, 2, 2};
    for (unsigned index = 0; index < sizeof rates / sizeof rates[0]; index++) {
        const AudioRate *rate = audio_clock_rate(rates[index]);
        CHECK(rate != NULL);
        CHECK_EQUAL(index + 1, rate->dac_byte);
        uint32_t divisor = i2s_divider_rounded(rate->mclk, rate->rate) + 1;
        CHECK_EQUAL(divisors[index], divisor);
        uint32_t nominal = rate->rate * 64 * divisor;
        CHECK(rate->mclk <= nominal);
        CHECK((uint64_t)(nominal - rate->mclk) * 1000000 <= (uint64_t)nominal * 70);
        if (rate->rate % 48000 == 0) {
            CHECK_EQUAL(nominal, rate->mclk);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(usb_rates_have_documented_dac_codes_and_bounded_clock_error),
        TEST_CASE(the_i2c_divider_matches_the_boot_clock_value),
        TEST_CASE(the_i2c_divider_follows_pclk),
        TEST_CASE(the_i2s_divider_truncates_or_rounds),
        TEST_CASE(the_i2s_clock_register_holds_the_divider),
        TEST_CASE(the_32_khz_clock_uses_an_exact_divider_and_the_documented_dac_rate),
        TEST_CASE(one_sine_period_starts_at_zero_and_peaks_at_a_quarter),
        TEST_CASE(the_sine_rises_then_falls),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
