#pragma once

#include <stdint.h>

enum {
    COLOR_ALPHA_OPAQUE = 15,
    COLOR_LIGHT_SHIFT = 8,
    COLOR_LIGHT_FULL = 1 << COLOR_LIGHT_SHIFT,
    COLOR_SPREAD_SHIFT = 16,
    COLOR_SPREAD_LIGHT_BITS = 5,
    COLOR_SPREAD_LIGHT_DROP = COLOR_LIGHT_SHIFT - COLOR_SPREAD_LIGHT_BITS,
    COLOR_SPREAD_MASK = 0x07E0F81F
};

typedef uint16_t Color;

#define COLOR_RGB(red, green, blue)                                                                \
    ((Color)((((red) & 0xF8) << 8) | (((green) & 0xFC) << 3) | ((blue) >> 3)))

Color color_from_rgb(uint8_t red, uint8_t green, uint8_t blue);
Color color_blend(Color background, Color foreground, uint8_t alpha);
Color color_between(Color from, Color to, int step, int steps);

static inline Color color_scale(Color color, unsigned light)
{
    if (light >= COLOR_LIGHT_FULL) {
        return color;
    }
    uint32_t spread =
        ((uint32_t)color | (uint32_t)color << COLOR_SPREAD_SHIFT) & (uint32_t)COLOR_SPREAD_MASK;
    uint32_t scaled = (spread * (light >> COLOR_SPREAD_LIGHT_DROP)) >> COLOR_SPREAD_LIGHT_BITS &
                      (uint32_t)COLOR_SPREAD_MASK;
    return (Color)(scaled | scaled >> COLOR_SPREAD_SHIFT);
}
