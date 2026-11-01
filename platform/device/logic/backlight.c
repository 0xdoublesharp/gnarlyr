#include "device/backlight.h"

enum {
    FULL_PERCENT = 100
};

static const uint8_t PERCENT_FOR_LEVEL[HAL_DISPLAY_BRIGHTNESS_MAX] = {8,  12, 17, 23, 30,
                                                                      39, 50, 63, 80, 100};

uint32_t backlight_percent(uint8_t level)
{
    if (level < HAL_DISPLAY_BRIGHTNESS_MIN) {
        level = HAL_DISPLAY_BRIGHTNESS_MIN;
    }
    if (level > HAL_DISPLAY_BRIGHTNESS_MAX) {
        level = HAL_DISPLAY_BRIGHTNESS_MAX;
    }
    return PERCENT_FOR_LEVEL[level - HAL_DISPLAY_BRIGHTNESS_MIN];
}

uint32_t backlight_duty(uint32_t period, uint32_t percent)
{
    if (percent >= FULL_PERCENT) {
        return 0;
    }
    return period - period * percent / FULL_PERCENT;
}

BacklightPwm backlight_pwm_at(uint32_t pclk_hz, uint32_t percent, uint32_t hz)
{
    if (percent >= FULL_PERCENT) {
        return (BacklightPwm){.full = true};
    }
    uint32_t period = pclk_hz / hz;
    return (BacklightPwm){.full = false, .period = period, .duty = backlight_duty(period, percent)};
}

BacklightPwm backlight_pwm(uint32_t pclk_hz, uint32_t percent)
{
    return backlight_pwm_at(pclk_hz, percent, BACKLIGHT_MINI_HZ);
}

uint32_t backlight_ramp_percent(uint32_t target_percent, uint32_t elapsed_ms)
{
    if (elapsed_ms >= BACKLIGHT_RAMP_MS) {
        return target_percent;
    }
    uint32_t percent = target_percent * elapsed_ms / BACKLIGHT_RAMP_MS;
    return percent > 0 ? percent : 1;
}
