#include "device/usb_level.h"

enum {
    TOP_BIT = 15,
    STEPS_PER_BIT = 8,
    FRACTION_SHIFT = 3,
    BITS_SHOWN = USB_LEVEL_RANGE_DB / 6,
    MS_PER_SECOND = 1000
};

static int top_bit(uint32_t value)
{
    int bit = 0;
    while ((value >> (bit + 1)) != 0) {
        bit++;
    }
    return bit;
}

uint8_t usb_level_of(uint16_t peak)
{
    if (peak == 0) {
        return 0;
    }
    int bit = top_bit(peak);
    int fraction = bit >= FRACTION_SHIFT ? (peak >> (bit - FRACTION_SHIFT)) & (STEPS_PER_BIT - 1)
                                         : (peak << (FRACTION_SHIFT - bit)) & (STEPS_PER_BIT - 1);
    int steps = (bit - (TOP_BIT - BITS_SHOWN)) * STEPS_PER_BIT + fraction;
    if (steps <= 0) {
        return 0;
    }
    int level = steps * USB_LEVEL_FULL / (BITS_SHOWN * STEPS_PER_BIT);
    return (uint8_t)(level > USB_LEVEL_FULL ? USB_LEVEL_FULL : level);
}

uint8_t usb_level_follow(uint8_t shown, uint8_t now, uint32_t elapsed_ms)
{
    if (now >= shown) {
        return now;
    }
    uint32_t fall = elapsed_ms * USB_LEVEL_FALL_PER_SECOND / MS_PER_SECOND;
    uint32_t lowered = fall >= shown ? 0 : shown - fall;
    return (uint8_t)(lowered > now ? lowered : now);
}
