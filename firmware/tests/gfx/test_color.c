#include "check.h"
#include "gfx/color.h"

static void white_sets_every_bit(void)
{
    CHECK_EQUAL(0xFFFF, color_from_rgb(255, 255, 255));
}

static void black_is_zero(void)
{
    CHECK_EQUAL(0x0000, color_from_rgb(0, 0, 0));
}

static void primaries_fill_their_rgb565_fields(void)
{
    CHECK_EQUAL(0xF800, color_from_rgb(255, 0, 0));
    CHECK_EQUAL(0x07E0, color_from_rgb(0, 255, 0));
    CHECK_EQUAL(0x001F, color_from_rgb(0, 0, 255));
}

static void bits_below_rgb565_precision_are_dropped(void)
{
    CHECK_EQUAL(0x0000, color_from_rgb(7, 3, 7));
    CHECK_EQUAL(0x0821, color_from_rgb(8, 4, 8));
}

static void zero_alpha_keeps_the_background(void)
{
    CHECK_EQUAL(0x1234, color_blend(0x1234, 0xFFFF, 0));
}

static void opaque_alpha_takes_the_foreground(void)
{
    CHECK_EQUAL(0xFFFF, color_blend(0x1234, 0xFFFF, COLOR_ALPHA_OPAQUE));
}

static void partial_alpha_mixes_each_channel(void)
{
    CHECK_EQUAL(0x8C51, color_blend(0x0000, 0xFFFF, 8));
    CHECK_EQUAL(0x8800, color_blend(0x0000, 0xF800, 8));
}

static void the_constant_macro_matches_the_function(void)
{
    CHECK_EQUAL(color_from_rgb(18, 52, 86), COLOR_RGB(18, 52, 86));
    CHECK_EQUAL(color_from_rgb(255, 128, 1), COLOR_RGB(255, 128, 1));
}

static void full_light_keeps_the_color(void)
{
    CHECK_EQUAL(0xFFFF, color_scale(0xFFFF, COLOR_LIGHT_FULL));
    CHECK_EQUAL(0x1234, color_scale(0x1234, COLOR_LIGHT_FULL));
}

static void no_light_is_black(void)
{
    CHECK_EQUAL(0x0000, color_scale(0xFFFF, 0));
}

static void half_light_halves_each_channel(void)
{
    CHECK_EQUAL(COLOR_RGB(120, 64, 32), color_scale(COLOR_RGB(255, 128, 64), COLOR_LIGHT_FULL / 2));
}

static unsigned reference_mix(unsigned background, unsigned foreground, unsigned alpha)
{
    return (background * (COLOR_ALPHA_OPAQUE - alpha) + foreground * alpha + 7) /
           COLOR_ALPHA_OPAQUE;
}

static Color reference_blend(Color background, Color foreground, unsigned alpha)
{
    unsigned red = reference_mix(background >> 11, foreground >> 11, alpha);
    unsigned green = reference_mix(background >> 5 & 0x3F, foreground >> 5 & 0x3F, alpha);
    unsigned blue = reference_mix(background & 0x1F, foreground & 0x1F, alpha);
    return (Color)(red << 11 | green << 5 | blue);
}

static void blending_rounds_each_channel_like_the_per_channel_formula(void)
{
    uint32_t seed = 12345;
    for (unsigned alpha = 0; alpha <= COLOR_ALPHA_OPAQUE; alpha++) {
        for (int sample = 0; sample < 4096; sample++) {
            seed = seed * 1664525u + 1013904223u;
            Color background = (Color)(seed >> 16);
            Color foreground = (Color)seed;
            CHECK_EQUAL(reference_blend(background, foreground, alpha),
                        color_blend(background, foreground, (uint8_t)alpha));
        }
        CHECK_EQUAL(reference_blend(0xFFFF, 0x0000, alpha),
                    color_blend(0xFFFF, 0x0000, (uint8_t)alpha));
        CHECK_EQUAL(reference_blend(0x0000, 0xFFFF, alpha),
                    color_blend(0x0000, 0xFFFF, (uint8_t)alpha));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(full_light_keeps_the_color),
        TEST_CASE(no_light_is_black),
        TEST_CASE(half_light_halves_each_channel),
        TEST_CASE(white_sets_every_bit),
        TEST_CASE(black_is_zero),
        TEST_CASE(primaries_fill_their_rgb565_fields),
        TEST_CASE(bits_below_rgb565_precision_are_dropped),
        TEST_CASE(zero_alpha_keeps_the_background),
        TEST_CASE(opaque_alpha_takes_the_foreground),
        TEST_CASE(partial_alpha_mixes_each_channel),
        TEST_CASE(the_constant_macro_matches_the_function),
        TEST_CASE(blending_rounds_each_channel_like_the_per_channel_formula),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
