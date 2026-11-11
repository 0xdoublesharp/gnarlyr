#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "settings/sound_shaping.h"

static void each_step_maps_to_the_documented_filter_and_response(void)
{
    static const uint16_t hz[] = {300, 6000, 80, 90, 2000, 12000, 350};
    static const uint16_t q[] = {70, 71, 71, 140, 70, 71, 90};
    static const uint8_t type[] = {PARAMETRIC_PEAK, PARAMETRIC_HIGH_SHELF, PARAMETRIC_LOW_SHELF,
                                   PARAMETRIC_PEAK, PARAMETRIC_PEAK,       PARAMETRIC_HIGH_SHELF,
                                   PARAMETRIC_PEAK};
    ParametricEq base = {0};
    for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
        for (int step = -5; step <= 5; step++) {
            SoundShaping shape = {.active = true};
            shape.positions[slider] = (int8_t)step;
            ParametricEq eq = {0};
            CHECK(sound_shaping_build(&eq, &base, &shape));
            CHECK_EQUAL(step == 0 ? 0 : 1, eq.count);
            CHECK_EQUAL(0, eq.preamp);
            if (step == 0) {
                continue;
            }
            const ParametricFilter *filter = &eq.filters[0];
            CHECK_EQUAL(hz[slider], filter->hz);
            CHECK_EQUAL(q[slider], filter->q);
            CHECK_EQUAL(type[slider], filter->type);
            CHECK_EQUAL((slider == 6 ? -10 : 10) * step, filter->gain);
            CHECK(filter->enabled);
            uint32_t reference = slider == 2 ? 20 : slider == 1 || slider == 5 ? 20000 : hz[slider];
            int response = parametric_response(&eq, reference, 48000);
            CHECK(filter->gain > 0 ? response > 0 : response < 0);
            int error = response - filter->gain;
            CHECK(error >= -5 && error <= 5);
        }
    }
}

static void stacking_is_atomic_and_respects_the_ten_filter_budget(void)
{
    ParametricEq base = {.count = 10, .preamp = -62};
    for (int index = 0; index < 10; index++) {
        base.filters[index] = (ParametricFilter){1000, 70, -10, PARAMETRIC_PEAK, true};
    }
    SoundShaping shape = {.active = true, .base_count = 10};
    ParametricEq result = {0};
    CHECK(sound_shaping_build(&result, &base, &shape));
    CHECK(parametric_same(&result, &base));
    shape.positions[0] = 1;
    ParametricEq kept = result;
    CHECK(!sound_shaping_build(&result, &base, &shape));
    CHECK(parametric_same(&result, &kept));
    shape.base_count = 9;
    CHECK(sound_shaping_build(&result, &base, &shape));
    CHECK_EQUAL(10, result.count);
    CHECK_EQUAL(-62, result.preamp);
    shape.positions[1] = 1;
    CHECK(!sound_shaping_build(&result, &base, &shape));
    shape.positions[0] = 6;
    CHECK(!sound_shaping_build(&result, &base, &shape));
}

static void automatic_headroom_covers_extremes_at_every_supported_rate(void)
{
    static const uint32_t rates[] = {32000, 44100, 48000, 88200, 96000};
    for (int sign = -1; sign <= 1; sign += 2) {
        SoundShaping shape = {.active = true};
        for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
            shape.positions[slider] = (int8_t)(5 * sign);
        }
        ParametricEq eq = {0};
        ParametricEq base = {0};
        CHECK(sound_shaping_build(&eq, &base, &shape));
        for (unsigned rate = 0; rate < sizeof rates / sizeof rates[0]; rate++) {
            uint16_t gain = parametric_pre_gain(&eq, rates[rate]);
            CHECK(gain < 32768);
            for (uint32_t hz = 20; hz < rates[rate] / 2; hz += 97) {
                int response = parametric_response(&eq, hz, rates[rate]);
                int preamp = parametric_decibels((uint64_t)gain * gain >> 10);
                CHECK(response + preamp <= 0);
            }
        }
    }
}

static void row_words_describe_both_directions_and_neutral(void)
{
    SoundShaping shape = {.active = true};
    char text[64];
    sound_shaping_words(&shape, text, sizeof text);
    CHECK(strcmp(text, i18n_text(STR_SHAPE_NEUTRAL)) == 0);
    shape.positions[0] = 2;
    shape.positions[1] = 1;
    sound_shaping_words(&shape, text, sizeof text);
    CHECK(strcmp(text, "Warm · Bright") == 0);
    shape.positions[0] = -2;
    shape.positions[1] = -1;
    sound_shaping_words(&shape, text, sizeof text);
    CHECK(strcmp(text, "Cool · Dark") == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(each_step_maps_to_the_documented_filter_and_response),
        TEST_CASE(stacking_is_atomic_and_respects_the_ten_filter_budget),
        TEST_CASE(automatic_headroom_covers_extremes_at_every_supported_rate),
        TEST_CASE(row_words_describe_both_directions_and_neutral),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
