#include "gfx/color.h"

enum {
    RED_MASK = 0xF8,
    GREEN_MASK = 0xFC,
    RED_SHIFT = 11,
    GREEN_SHIFT = 5,
    RGB_RED_SHIFT = 8,
    RGB_GREEN_SHIFT = 3,
    RGB_BLUE_SHIFT = 3,
    FIVE_BIT_MASK = 0x1F,
    SIX_BIT_MASK = 0x3F,
    SPREAD_GREEN_SHIFT = 21,
    SPREAD_LOW_FIELD = 0x7FF,
    SPREAD_RED_FIELD = 0x3FF,
    SPREAD_ROUNDING = 7 << 21 | 7 << 11 | 7,
    FIFTEENTH_MULTIPLIER = 0x8889,
    FIFTEENTH_SHIFT = 19
};

typedef struct Channels {
    unsigned red;
    unsigned green;
    unsigned blue;
} Channels;

static Channels split(Color color)
{
    return (Channels){
        .red = (color >> RED_SHIFT) & FIVE_BIT_MASK,
        .green = (color >> GREEN_SHIFT) & SIX_BIT_MASK,
        .blue = color & FIVE_BIT_MASK,
    };
}

static Color join(Channels channels)
{
    return (Color)((channels.red << RED_SHIFT) | (channels.green << GREEN_SHIFT) | channels.blue);
}

Color color_from_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    unsigned red_bits = (unsigned)(red & RED_MASK) << RGB_RED_SHIFT;
    unsigned green_bits = (unsigned)(green & GREEN_MASK) << RGB_GREEN_SHIFT;
    unsigned blue_bits = (unsigned)blue >> RGB_BLUE_SHIFT;
    return (Color)(red_bits | green_bits | blue_bits);
}

static uint32_t spread_of(Color color)
{
    return ((uint32_t)color | (uint32_t)color << COLOR_SPREAD_SHIFT) & (uint32_t)COLOR_SPREAD_MASK;
}

static uint32_t fifteenth(uint32_t value)
{
    return value * FIFTEENTH_MULTIPLIER >> FIFTEENTH_SHIFT;
}

Color color_blend(Color background, Color foreground, uint8_t alpha)
{
    uint32_t weight = alpha < COLOR_ALPHA_OPAQUE ? alpha : COLOR_ALPHA_OPAQUE;
    uint32_t mixed = spread_of(background) * (COLOR_ALPHA_OPAQUE - weight) +
                     spread_of(foreground) * weight + SPREAD_ROUNDING;
    uint32_t blue = fifteenth(mixed & SPREAD_LOW_FIELD);
    uint32_t red = fifteenth(mixed >> RED_SHIFT & SPREAD_RED_FIELD);
    uint32_t green = fifteenth(mixed >> SPREAD_GREEN_SHIFT & SPREAD_LOW_FIELD);
    return (Color)(red << RED_SHIFT | green << GREEN_SHIFT | blue);
}

static unsigned channel_between(unsigned from, unsigned to, int step, int steps)
{
    int difference = (int)to - (int)from;
    return (unsigned)((int)from + difference * step / steps);
}

Color color_between(Color from, Color to, int step, int steps)
{
    if (steps <= 0) {
        return to;
    }
    Channels first = split(from);
    Channels last = split(to);
    return join((Channels){
        .red = channel_between(first.red, last.red, step, steps),
        .green = channel_between(first.green, last.green, step, steps),
        .blue = channel_between(first.blue, last.blue, step, steps),
    });
}
