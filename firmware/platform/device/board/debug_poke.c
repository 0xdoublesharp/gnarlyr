#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "core/debug_script.h"
#include "debug_probe.h"
#include "gpio.h"
#include "hal/system.h"
#include "registers.h"
#include "usb.h"

enum {
    BLOCK_BYTES = 0x400,
    WORD_BYTES = 4,
    PORT_COUNT = 3,
    PIN_MASK = 0xFF
};

static bool word_allowed(uint32_t address)
{
    static const uint32_t BLOCKS[] = {GPIO0_BASE, GPIO1_BASE, GPIO2_BASE, GRF_BASE, CRU_BASE};
    for (uint32_t block = 0; block < sizeof BLOCKS / sizeof BLOCKS[0]; block++) {
        if (address - BLOCKS[block] < BLOCK_BYTES) {
            return address % WORD_BYTES == 0;
        }
    }
    return false;
}

static void probe_word(HalProbe *probe, uint32_t address, const uint32_t *value)
{
    probe->allowed = word_allowed(address);
    if (!probe->allowed) {
        return;
    }
    probe->before = register_read(address);
    if (value != NULL) {
        register_write(address, *value);
    }
    probe->after = register_read(address);
}

static uint32_t pin_state(GpioPin pin)
{
    return gpio_function(pin) << DEBUG_PIN_STATE_FUNCTION_SHIFT |
           (gpio_is_output(pin) ? DEBUG_PIN_STATE_OUTPUT : 0u) | (gpio_read(pin) ? 1u : 0u);
}

static void act_on_pin(GpioPin pin, uint32_t action)
{
    uint32_t what = action & PIN_MASK;
    if (what == DEBUG_PIN_FUNCTION) {
        gpio_mux(pin, action >> DEBUG_PIN_FUNCTION_SHIFT);
    } else if (what != DEBUG_PIN_READ) {
        gpio_mux(pin, GPIO_FUNCTION_GPIO);
    }
    if (what == DEBUG_PIN_INPUT) {
        gpio_input(pin);
    } else if (what == DEBUG_PIN_LOW || what == DEBUG_PIN_HIGH) {
        gpio_output(pin, what == DEBUG_PIN_HIGH);
    }
}

static void probe_pin(HalProbe *probe, uint32_t where, uint32_t action)
{
    GpioPin pin = {(uint8_t)(where >> DEBUG_PIN_PORT_SHIFT), (uint8_t)(where & PIN_MASK)};
    probe->allowed = pin.port < PORT_COUNT && pin.pin < DEBUG_PIN_COUNT;
    if (!probe->allowed) {
        return;
    }
    probe->before = pin_state(pin);
    act_on_pin(pin, action);
    probe->after = pin_state(pin);
}

uint8_t hal_system_power_levels(void)
{
    BoardChargePin charge = board_power_charge_pin();
    return (uint8_t)((usb_vbus_present() ? HAL_PROBE_CABLE : 0) |
                     (charge.ready ? HAL_PROBE_CHARGE_KNOWN : 0) |
                     (charge.high ? HAL_PROBE_CHARGE_PIN : 0) |
                     (board_power_usb_supply_held_off() ? HAL_PROBE_SUPPLY_OFF : 0));
}

void hal_system_hold_usb_supply_off(bool held)
{
    board_power_usb_supply_hold_off(held);
}

HalProbe hal_system_probe(HalProbeKind kind, uint32_t first, uint32_t second)
{
    HalProbe probe = {.allowed = false};
    if (kind == HAL_PROBE_PIN) {
        probe_pin(&probe, first, second);
    } else {
        probe_word(&probe, first, kind == HAL_PROBE_POKE ? &second : NULL);
    }
    probe.levels = hal_system_power_levels();
    return probe;
}
