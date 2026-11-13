#include <stddef.h>

#include "board.h"
#include "cru.h"
#include "device/panel.h"
#include "device/vop.h"
#include "dmac.h"
#include "gpio.h"
#include "hal/audio.h"
#include "hal/clock.h"
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
    RESET_SETTLE_MS = 30,
    PUSH_MIN_PIXELS = 256,
    PUSH_BLOCK_WORDS = 2048,
    PUSH_TIMEOUT_US = 8000,
    PUSH_DRAIN_US = 20
};

static const GpioPin PANEL_RESET = {2, 15};
static bool controller_running;
static bool push_failed;
static uint32_t push_fallbacks;
static bool controller_gated;

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

static void bus_serviced_delay(void *context, uint32_t milliseconds)
{
    (void)context;
    uint32_t start = hal_clock_milliseconds();
    while (hal_clock_milliseconds() - start < milliseconds) {
        hal_audio_service();
        hal_system_keep_alive();
    }
}

static const PanelBus BUS = {bus_command, bus_data, bus_pixels, bus_delay, NULL};
static const PanelBus SERVICED_BUS = {bus_command, bus_data, bus_pixels, bus_serviced_delay, NULL};

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

void board_panel_sleep(void)
{
    panel_sleep(&SERVICED_BUS);
    cru_gate(VOP_GATE, false);
    controller_gated = true;
}

void board_panel_wake(void)
{
    if (controller_gated) {
        cru_gate(VOP_GATE, true);
        controller_gated = false;
        vop_set_clock(vop(), hal_system_clock_mhz());
    }
    panel_wake(&SERVICED_BUS);
}

void board_panel_set_rotation(PanelRotation rotation)
{
    panel_set_rotation(&BUS, rotation);
}

static bool wait_for_push(void)
{
    uint32_t start = hal_clock_microseconds();
    while (dmac_push_busy()) {
        if (hal_clock_microseconds() - start >= PUSH_TIMEOUT_US) {
            return false;
        }
        dmac_push_wait_event();
    }
    board_note_idle_us(hal_clock_microseconds() - start);
    return true;
}

static bool push_blocks(const uint16_t *pixels, uint32_t words)
{
    uint32_t source = (uint32_t)(uintptr_t)pixels;
    while (words > 0) {
        uint32_t block = words < PUSH_BLOCK_WORDS ? words : PUSH_BLOCK_WORDS;
        dmac_push_start(source, VOP_BASE + VOP_DATA * 4u, block);
        if (!wait_for_push()) {
            return false;
        }
        source += block * 4u;
        words -= block;
    }
    return true;
}

static bool push_by_dma(PanelWindow window, const uint16_t *pixels, uint32_t count)
{
    if (push_failed || count < PUSH_MIN_PIXELS || ((uintptr_t)pixels & 3u) != 0 ||
        !dmac_present()) {
        return false;
    }
    panel_begin_write(&BUS, window);
    if (!vop_begin_fifo(vop(), (uint32_t)window.width, (uint32_t)window.height)) {
        return false;
    }
    if (push_blocks(pixels, count / 2u)) {
        board_delay_us(PUSH_DRAIN_US);
        vop_end_fifo(vop());
        return true;
    }
    dmac_push_cancel();
    vop()[VOP_SOFT_RESET] = 1;
    vop_start(vop(), hal_system_clock_mhz());
    push_failed = true;
    push_fallbacks++;
    return false;
}

void board_panel_write(PanelWindow window, const uint16_t *pixels)
{
    uint32_t count = (uint32_t)(window.width * window.height);
    if (!push_by_dma(window, pixels, count)) {
        panel_begin_write(&BUS, window);
        vop_pixels(vop(), pixels, count);
    }
}

void board_panel_after_clock_change(void)
{
    if (controller_running && !controller_gated) {
        vop_set_clock(vop(), hal_system_clock_mhz());
    }
}

uint32_t board_panel_push_fallbacks(void)
{
    return push_fallbacks;
}
