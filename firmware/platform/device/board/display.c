#include <stddef.h>

#include "board.h"
#include "cru.h"
#include "device/backlight.h"
#include "gpio.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "panel_port.h"
#include "registers.h"

#define PWM_COUNTER (PWM_BASE + 0x0u)
#define PWM_PERIOD (PWM_BASE + 0x4u)
#define PWM_DUTY (PWM_BASE + 0x8u)
#define PWM_CONTROL (PWM_BASE + 0xCu)
#define PWM_CONTROL_RUN 0x00010003u

enum {
    CLEAR_ROWS = 1,
    PWM_GATE = 0x90,
    PWM_RESET = 0x17,
    PWM_RESET_MS = 1
};

static const GpioPin BACKLIGHT = {2, 4};

static PanelRotation rotation;
static uint8_t brightness;
static bool backlight_on;
static bool panel_asleep;
static bool pwm_running;
static uint32_t pwm_period;
static bool ramping;
static uint32_t ramp_started_ms;

static void wake_panel(void)
{
    if (panel_asleep) {
        panel_asleep = false;
        board_panel_wake();
    }
}

void board_display_clear(void)
{
    static uint16_t black[HAL_DISPLAY_WIDTH * CLEAR_ROWS];
    for (int y = 0; y < HAL_DISPLAY_HEIGHT; y += CLEAR_ROWS) {
        int rows = HAL_DISPLAY_HEIGHT - y < CLEAR_ROWS ? HAL_DISPLAY_HEIGHT - y : CLEAR_ROWS;
        hal_display_write(0, y, HAL_DISPLAY_WIDTH, rows, black);
    }
}

static void stop_pwm(void)
{
    if (!pwm_running) {
        return;
    }
    register_write(PWM_CONTROL, 0);
    cru_gate(PWM_GATE, false);
    pwm_running = false;
}

static void drive_backlight(bool level)
{
    stop_pwm();
    gpio_mux(BACKLIGHT, GPIO_FUNCTION_GPIO);
    gpio_output(BACKLIGHT, level);
}

static void start_pwm(void)
{
    cru_gate(PWM_GATE, true);
    cru_reset(PWM_RESET, true);
    board_delay_ms(PWM_RESET_MS);
    cru_reset(PWM_RESET, false);
    pwm_running = true;
}

static void run_pwm(BacklightPwm pwm)
{
    if (!pwm_running) {
        start_pwm();
    }
    register_write(PWM_CONTROL, 0);
    register_write(PWM_DUTY, pwm.duty);
    register_write(PWM_COUNTER, 0);
    register_write(PWM_PERIOD, pwm.period);
    register_write(PWM_CONTROL, PWM_CONTROL_RUN);
    pwm_period = pwm.period;
    gpio_output(BACKLIGHT, true);
    gpio_mux(BACKLIGHT, GPIO_FUNCTION_ALTERNATE);
}

static void apply_backlight(void)
{
    if (!backlight_on) {
        drive_backlight(false);
        return;
    }
    BacklightPwm pwm = backlight_pwm(board_pclk_hz(), backlight_percent(brightness));
    if (pwm.full) {
        drive_backlight(true);
    } else {
        run_pwm(pwm);
    }
}

bool board_display_lit(void)
{
    return backlight_on;
}

void board_backlight_full(void)
{
    ramping = false;
    backlight_on = true;
    drive_backlight(true);
}

static void start_fade_in(void)
{
    ramping = true;
    ramp_started_ms = hal_clock_milliseconds();
    run_pwm(backlight_pwm(board_pclk_hz(), backlight_ramp_percent(0, 0)));
}

void board_display_service(void)
{
    if (!ramping) {
        return;
    }
    uint32_t elapsed_ms = hal_clock_milliseconds() - ramp_started_ms;
    if (elapsed_ms >= BACKLIGHT_RAMP_MS || !pwm_running) {
        ramping = false;
        apply_backlight();
        return;
    }
    uint32_t percent = backlight_ramp_percent(backlight_percent(brightness), elapsed_ms);
    register_write(PWM_DUTY, backlight_duty(pwm_period, percent));
}

void board_backlight_off(void)
{
    ramping = false;
    backlight_on = false;
    drive_backlight(false);
}

void board_display_after_clock_change(void)
{
    board_panel_after_clock_change();
    if (pwm_running) {
        apply_backlight();
    }
}

void board_display_start(void)
{
    brightness = HAL_DISPLAY_BRIGHTNESS_MAX;
    backlight_on = false;
    drive_backlight(false);
    board_panel_start();
    board_display_clear();
}

void hal_display_set_rotation(HalDisplayRotation turned)
{
    if ((PanelRotation)turned == rotation) {
        return;
    }
    rotation = (PanelRotation)turned;
    wake_panel();
    board_panel_set_rotation(rotation);
}

void hal_display_write(int x, int y, int width, int height, const uint16_t *pixels)
{
    wake_panel();
    board_panel_write((PanelWindow){x, y, width, height, rotation}, pixels);
}

void hal_display_present(void)
{
}

void hal_display_set_power(bool on)
{
    board_start_record_step(on ? HAL_START_STEP_BACKLIGHT_ON : HAL_START_STEP_BACKLIGHT_OFF);
    bool was_on = backlight_on;
    if (on) {
        board_power_pair_set(true);
    }
    backlight_on = on;
    ramping = false;
    if (on) {
        wake_panel();
    }
    if (on && !was_on) {
        start_fade_in();
    } else {
        apply_backlight();
    }
    if (!on && !panel_asleep) {
        panel_asleep = true;
        board_panel_sleep();
    }
    board_start_record_step_done();
}

void hal_display_set_brightness(uint8_t level)
{
    brightness = level;
    if (backlight_on && !ramping) {
        apply_backlight();
    }
}
