#include <stddef.h>

#include "board.h"
#include "cru.h"
#include "device/panel.h"
#include "device/vop.h"
#include "gpio.h"
#include "hal/system.h"
#include "panel_port.h"
#include "registers.h"

#define VOP_BASE 0x60070000u

enum {
    VOP_GATE = 0x7F,
    VOP_RESET = 0x0D,
    VOP_RESET_HOLD_US = 10,
    VOP_RESET_SETTLE_MS = 2,
    PANEL_FIRST_PIN = 15,
    PANEL_LAST_PIN = 25,
    RESET_HIGH_MS = 50,
    RESET_LOW_MS = 150,
    RESET_SETTLE_MS = 30
};

static const GpioPin PANEL_RESET = {2, 15};
static bool controller_running;

static volatile uint32_t *vop(void)
{
    return register_at(VOP_BASE);
}

static void bus_command(void *context, uint8_t command)
{
    (void)context;
    vop_command(vop(), command);
}

static void bus_data(void *context, uint8_t value)
{
    (void)context;
    vop_data(vop(), value);
}

static void bus_pixels(void *context, const uint16_t *pixels, uint32_t count)
{
    (void)context;
    vop_pixels(vop(), pixels, count);
}

static void bus_delay(void *context, uint32_t milliseconds)
{
    (void)context;
    board_delay_ms(milliseconds);
}

static const PanelBus BUS = {bus_command, bus_data, bus_pixels, bus_delay, NULL};

static void power_controller(void)
{
    cru_gate(VOP_GATE, true);
    cru_reset(VOP_RESET, true);
    board_delay_us(VOP_RESET_HOLD_US);
    cru_reset(VOP_RESET, false);
    board_delay_ms(VOP_RESET_SETTLE_MS);
    vop_start(vop(), hal_system_clock_mhz());
    controller_running = true;
}

static void mux_panel_pins(void)
{
    for (uint8_t pin = PANEL_FIRST_PIN; pin <= PANEL_LAST_PIN; pin++) {
        gpio_mux((GpioPin){0, pin}, GPIO_FUNCTION_ALTERNATE);
    }
    gpio_mux(PANEL_RESET, GPIO_FUNCTION_GPIO);
}

static void pulse_reset(void)
{
    gpio_output(PANEL_RESET, true);
    board_delay_ms(RESET_HIGH_MS);
    gpio_output(PANEL_RESET, false);
    board_delay_ms(RESET_LOW_MS);
    gpio_output(PANEL_RESET, true);
    board_delay_ms(RESET_SETTLE_MS);
}

void board_panel_start(void)
{
    power_controller();
    mux_panel_pins();
    pulse_reset();
    panel_send_init(&BUS);
}

void board_panel_write(PanelWindow window, const uint16_t *pixels)
{
    panel_begin_write(&BUS, window);
    vop_pixels(vop(), pixels, (uint32_t)(window.width * window.height));
}

void board_panel_after_clock_change(void)
{
    if (controller_running) {
        vop_set_clock(vop(), hal_system_clock_mhz());
    }
}
