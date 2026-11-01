#include "check.h"
#include "device/backlight.h"

static void levels_get_brighter_up_to_full_at_the_top(void)
{
    uint32_t previous = 0;
    for (uint8_t level = HAL_DISPLAY_BRIGHTNESS_MIN; level <= HAL_DISPLAY_BRIGHTNESS_MAX; level++) {
        uint32_t percent = backlight_percent(level);
        CHECK(percent > previous);
        previous = percent;
    }
    CHECK_EQUAL(100u, backlight_percent(HAL_DISPLAY_BRIGHTNESS_MAX));
}

static void the_dimmest_level_keeps_the_panel_lit(void)
{
#ifdef ECHO_BOARD_ECHO
    CHECK_EQUAL(20u, backlight_percent(HAL_DISPLAY_BRIGHTNESS_MIN));
#else
    CHECK_EQUAL(8u, backlight_percent(HAL_DISPLAY_BRIGHTNESS_MIN));
#endif
}

static void levels_outside_the_range_are_clamped(void)
{
    CHECK_EQUAL(backlight_percent(HAL_DISPLAY_BRIGHTNESS_MIN), backlight_percent(0));
    CHECK_EQUAL(100u, backlight_percent(200));
}

static void the_pwm_runs_at_ten_kilohertz_from_pclk(void)
{
    BacklightPwm pwm = backlight_pwm(48000000u, 47);
    CHECK(!pwm.full);
    CHECK_EQUAL(4800u, pwm.period);
    pwm = backlight_pwm(24000000u, 47);
    CHECK_EQUAL(2400u, pwm.period);
}

static void the_echo_pwm_runs_at_twenty_five_kilohertz_from_pclk(void)
{
    CHECK_EQUAL(25000u, BACKLIGHT_ECHO_HZ);
    BacklightPwm pwm = backlight_pwm_at(50000000u, 47, BACKLIGHT_ECHO_HZ);
    CHECK(!pwm.full);
    CHECK_EQUAL(2000u, pwm.period);
    CHECK_EQUAL(1060u, pwm.duty);
}

static void the_mini_frequency_is_unchanged(void)
{
    CHECK_EQUAL(backlight_pwm(48000000u, 47).period,
                backlight_pwm_at(48000000u, 47, BACKLIGHT_MINI_HZ).period);
}

static void the_duty_register_counts_the_time_the_backlight_is_off(void)
{
    CHECK_EQUAL(2544u, backlight_pwm(48000000u, 47).duty);
    CHECK_EQUAL(1272u, backlight_pwm(24000000u, 47).duty);
    CHECK_EQUAL(2208u, backlight_duty(2400u, 8));
    CHECK_EQUAL(480u, backlight_duty(2400u, 80));
    CHECK_EQUAL(0u, backlight_duty(2400u, 100));
}

static void brighter_levels_hold_the_pin_low_for_less_of_the_period(void)
{
    uint32_t previous = UINT32_MAX;
    for (uint8_t level = HAL_DISPLAY_BRIGHTNESS_MIN; level < HAL_DISPLAY_BRIGHTNESS_MAX; level++) {
        uint32_t duty = backlight_pwm(24000000u, backlight_percent(level)).duty;
        CHECK(duty < previous);
        previous = duty;
    }
}

static void full_brightness_skips_the_pwm(void)
{
    BacklightPwm pwm = backlight_pwm(50000000u, 100);
    CHECK(pwm.full);
}

static void the_fade_in_rises_from_dim_to_the_target(void)
{
    CHECK_EQUAL(1u, backlight_ramp_percent(50, 0));
    CHECK_EQUAL(25u, backlight_ramp_percent(50, BACKLIGHT_RAMP_MS / 2));
    CHECK_EQUAL(50u, backlight_ramp_percent(50, BACKLIGHT_RAMP_MS));
    CHECK_EQUAL(50u, backlight_ramp_percent(50, BACKLIGHT_RAMP_MS * 3));
}

static void the_fade_in_to_full_stays_on_the_pwm_until_it_ends(void)
{
    CHECK(backlight_ramp_percent(100, BACKLIGHT_RAMP_MS - 1) < 100u);
    CHECK_EQUAL(100u, backlight_ramp_percent(100, BACKLIGHT_RAMP_MS));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(levels_get_brighter_up_to_full_at_the_top),
        TEST_CASE(the_dimmest_level_keeps_the_panel_lit),
        TEST_CASE(levels_outside_the_range_are_clamped),
        TEST_CASE(the_pwm_runs_at_ten_kilohertz_from_pclk),
        TEST_CASE(the_echo_pwm_runs_at_twenty_five_kilohertz_from_pclk),
        TEST_CASE(the_mini_frequency_is_unchanged),
        TEST_CASE(the_duty_register_counts_the_time_the_backlight_is_off),
        TEST_CASE(brighter_levels_hold_the_pin_low_for_less_of_the_period),
        TEST_CASE(full_brightness_skips_the_pwm),
        TEST_CASE(the_fade_in_rises_from_dim_to_the_target),
        TEST_CASE(the_fade_in_to_full_stays_on_the_pwm_until_it_ends),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
