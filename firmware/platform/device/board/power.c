#include "hal/power.h"
#include "board.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "gpio.h"
#include "hal/clock.h"
#include "registers.h"
#include "usb.h"

#define GRF_PORT2_LOW_PULL (GRF_BASE + 0x50u)
#define POWER_KEY_PULL_OFF 0x00200000u

enum {
    DAC_DISCHARGE_MS = 1000
};

static const GpioPin POWER_KEY = {2, 5};
static const GpioPin POWER_HOLD = {2, 6};
static const GpioPin CHARGE_STATUS = {2, 3};

static bool charge_status_ready;
static HalPowerHold boot_hold;
static bool usb_start;

void board_power_hold(void)
{
    boot_hold = (HalPowerHold){.function = (uint8_t)gpio_function(POWER_HOLD),
                               .output = gpio_is_output(POWER_HOLD),
                               .high = gpio_output_level(POWER_HOLD)};
    gpio_mux(POWER_HOLD, GPIO_FUNCTION_GPIO);
    gpio_output(POWER_HOLD, true);
    gpio_mux(POWER_KEY, GPIO_FUNCTION_GPIO);
    gpio_input(POWER_KEY);
    register_write(GRF_PORT2_LOW_PULL, POWER_KEY_PULL_OFF);
    boot_hold.usb_power = usb_vbus_present();
    boot_hold.key_down = gpio_read(POWER_KEY);
    bool held_by_key = boot_hold.output && boot_hold.high;
    usb_start = boot_hold.usb_power && !boot_hold.key_down && !held_by_key;
}

static bool usb_supply_known;
static uint8_t usb_supply_on;
static bool usb_supply_requested;
static uint8_t battery_care;
static uint8_t battery_care_saved;
static bool battery_care_cable;
static ChargeTracker charge;

static void usb_supply_pins_set(uint8_t flags)
{
    PinRef pins[BOARD_POWER_PAIR_PINS];
    uint32_t count = board_power_pair(pins);
    for (uint32_t index = 0; index < count; index++) {
        GpioPin pin = {pins[index].port, pins[index].pin};
        gpio_mux(pin, GPIO_FUNCTION_GPIO);
        gpio_output(pin, (flags & (1u << index)) != 0);
    }
}

void board_power_usb_supply_follow(bool cable)
{
    if (cable != battery_care_cable) {
        battery_care_cable = cable;
        battery_care = battery_care_saved;
    }
    usb_supply_requested = cable;
    BatteryGauge gauge = board_battery_gauge();
    bool was_charging = (usb_supply_on & 2u) == 0 || (usb_supply_on & 1u) != 0;
    bool charging = battery_care_charge(
        was_charging,
        (BatteryCareReading){.usb_power = cable,
                             .mode = battery_care,
                             .known = gauge.count >= BATTERY_RECENT_READINGS,
                             .millivolts = (uint16_t)battery_gauge_recent_millivolts(&gauge)});
    uint8_t flags = cable ? (uint8_t)(2u | (charging ? 1u : 0u)) : 0;
    if (usb_supply_known && usb_supply_on == flags) {
        return;
    }
    charge_tracker_init(&charge);
    usb_supply_known = true;
    usb_supply_on = flags;
    usb_supply_pins_set(flags);
}

void hal_power_set_battery_care(uint8_t mode)
{
    battery_care_saved = mode;
    battery_care = mode;
}

void hal_power_use_battery_care(uint8_t mode)
{
    battery_care = mode;
}

uint8_t hal_power_battery_care(void)
{
    return battery_care;
}

HalPowerHold hal_power_boot_hold(void)
{
    return boot_hold;
}

bool hal_power_cable(void)
{
    return usb_vbus_present();
}

bool hal_power_take_usb_start(void)
{
    bool started = usb_start;
    usb_start = false;
    return started;
}
static BatteryDisplay display;
static bool usb_seen;
static bool usb_before;

static bool charge_status_high(void)
{
    if (!charge_status_ready) {
        gpio_mux(CHARGE_STATUS, GPIO_FUNCTION_GPIO);
        gpio_input(CHARGE_STATUS);
        charge_status_ready = true;
    }
    return gpio_read(CHARGE_STATUS);
}

HalBattery hal_power_battery(void)
{
    bool usb_power = usb_vbus_present() && !board_debug_usb_supply_held_off();
    if (usb_seen && usb_power != usb_before) {
        board_battery_restart();
    }
    usb_seen = true;
    usb_before = usb_power;
    BatteryGauge gauge = board_battery_gauge();
    board_power_usb_supply_follow(usb_supply_requested);
    bool disabled = usb_power && usb_supply_known && (usb_supply_on & 1u) == 0;
    bool status_high = charge_status_high();
    ChargeState state = charge_tracker_update(
        &charge, (ChargeReading){.usb_power = usb_power,
                                 .status_high = status_high,
                                 .millivolts = battery_gauge_recent_millivolts(&gauge),
                                 .now_ms = hal_clock_milliseconds()});
    bool charging = !disabled && state == CHARGE_CHARGING;
    if (!battery_gauge_known(&gauge)) {
        return (HalBattery){.known = false, .usb_power = usb_power, .charging = charging};
    }
    uint8_t percent = BATTERY_FULL_PERCENT;
    if (state == CHARGE_FULL && !disabled) {
        battery_display_show_full(&display);
    } else {
        bool current_flowing = charging && !status_high;
        percent = battery_display_update(&display, battery_gauge_percent(&gauge, current_flowing),
                                         usb_power);
    }
    return (HalBattery){.known = true,
                        .percent = percent,
                        .charge_disabled = disabled,
                        .charging = charging,
                        .usb_power = usb_power,
                        .millivolts = (uint16_t)battery_gauge_recent_millivolts(&gauge)};
}

BoardChargePin board_power_charge_pin(void)
{
    return (BoardChargePin){.ready = charge_status_ready,
                            .high = charge_status_ready && gpio_read(CHARGE_STATUS)};
}

BatteryDisplay board_power_shown(void)
{
    return display;
}

CORE_SEAM void board_power_cut(void)
{
    board_backlight_off();
    board_bluetooth_off();
    __asm__ volatile("cpsid i" ::: "memory");
    board_start_record_power_off();
    usb_supply_pins_set(false);
    gpio_mux(POWER_HOLD, GPIO_FUNCTION_GPIO);
    gpio_output(POWER_HOLD, false);
    for (;;) {
        board_watchdog_kick();
    }
}

bool board_power_key_down(void)
{
    return gpio_read(POWER_KEY);
}

CORE_SEAM void hal_power_off(void)
{
    board_stall_pause(true);
    board_audio_shut_down();
    board_debug_power_off();
    board_delay_ms(DAC_DISCHARGE_MS);
    while (gpio_read(POWER_KEY)) {
    }
    board_power_cut();
}
