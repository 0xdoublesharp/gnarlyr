#include "hal/input.h"
#include "board.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "device/battery_gauge.h"
#include "device/headset_monitor.h"
#include "device/key_ladder.h"
#include "device/key_scanner.h"
#include "gpio.h"
#include "hal/clock.h"
#include "irq.h"
#include "registers.h"

#define ADC_RESULT (SARADC_BASE + 0x00u)
#define ADC_CONTROL (SARADC_BASE + 0x08u)
#define ADC_RESULT_MASK 0x3FFu
#define ADC_CHANNEL_MASK 0x7u
#define ADC_KEY_CHANNEL 0x1u
#define ADC_BATTERY_CHANNEL 0x0u
#define ADC_HEADSET_CHANNEL 0x2u
#define ADC_POWER_ON (1u << 3)
#define ADC_START (1u << 4)
#define ADC_INTERRUPT_STATUS (1u << 6)

static const GpioPin POWER_KEY = {2, 5};

enum {
    SCANS_PER_BATTERY_READING = 200,
    SCANS_PER_HEADSET_READING = 4,
    SCANS_PER_WATCHED_KEY_READING = 4,
    WATCHED_SCANS = 400,
    BOOT_READING_WAIT_MS = 2
};

static uint16_t key_adc;

uint16_t hal_input_key_adc(void)
{
    return key_adc;
}

static KeyScanner scanner;
static BatteryGauge gauge;
static HeadsetMonitor headset;
static uint32_t watched_scans;
static bool conversion_started;
static uint32_t pending_channel;
static uint32_t scans;

static void start_conversion(uint32_t channel)
{
    uint32_t control = register_read(ADC_CONTROL) & ~ADC_START;
    register_write(ADC_CONTROL, control);
    control &= ~(ADC_CHANNEL_MASK | ADC_INTERRUPT_STATUS);
    register_write(ADC_CONTROL, control | channel | ADC_POWER_ON | ADC_START);
}

static uint8_t pressed_keys(uint16_t reading)
{
    uint8_t held = (uint8_t)(scanner.stable & ~(1u << KEY_POWER));
    uint8_t mask = key_ladder_held_mask(reading, held);
    return gpio_read(POWER_KEY) ? (uint8_t)(mask | (1u << KEY_POWER)) : mask;
}

static void record_battery(void)
{
    if (battery_gauge_known(&gauge)) {
        board_start_record_battery(battery_gauge_millivolts(&gauge));
    }
}

uint16_t board_battery_boot_reading(void)
{
    start_conversion(ADC_BATTERY_CHANNEL);
    board_delay_ms(BOOT_READING_WAIT_MS);
    return (uint16_t)(register_read(ADC_RESULT) & ADC_RESULT_MASK);
}

void board_input_start(void)
{
    gpio_mux(POWER_KEY, GPIO_FUNCTION_GPIO);
    gpio_input(POWER_KEY);
    key_scanner_init(&scanner);
    battery_gauge_init(&gauge);
    headset_monitor_init(&headset);
    pending_channel = ADC_KEY_CHANNEL;
    start_conversion(ADC_KEY_CHANNEL);
    conversion_started = true;
}

static void take_headset_reading(uint16_t reading, uint32_t now_ms)
{
    headset_monitor_sample(&headset, reading, board_jack_headset_ready(), now_ms);
}

static void take_reading(uint16_t reading, uint32_t now_ms)
{
    switch (pending_channel) {
    case ADC_BATTERY_CHANNEL:
        battery_gauge_add(&gauge, reading);
        board_debug_battery_reading(reading);
        record_battery();
        break;
    case ADC_HEADSET_CHANNEL:
        take_headset_reading(reading, now_ms);
        break;
    default:
        key_adc = reading;
        key_scanner_sample(&scanner, pressed_keys(reading), now_ms);
        break;
    }
}

static uint32_t next_channel(void)
{
    scans++;
    if (scans % SCANS_PER_BATTERY_READING == 0) {
        return ADC_BATTERY_CHANNEL;
    }
    if (watched_scans > 0) {
        watched_scans--;
        return scans % SCANS_PER_WATCHED_KEY_READING == 0 ? ADC_KEY_CHANNEL : ADC_HEADSET_CHANNEL;
    }
    return scans % SCANS_PER_HEADSET_READING == 0 ? ADC_HEADSET_CHANNEL : ADC_KEY_CHANNEL;
}

void board_input_sample(uint32_t now_ms)
{
    if (!conversion_started) {
        return;
    }
    take_reading((uint16_t)(register_read(ADC_RESULT) & ADC_RESULT_MASK), now_ms);
    pending_channel = next_channel();
    start_conversion(pending_channel);
}

void board_battery_restart(void)
{
    uint32_t primask = irq_save();
    battery_gauge_restart(&gauge);
    irq_restore(primask);
}

BatteryGauge board_battery_gauge(void)
{
    uint32_t primask = irq_save();
    BatteryGauge copy = gauge;
    irq_restore(primask);
    return copy;
}

bool hal_input_next_transition(HalKeyTransition *transition)
{
    uint32_t primask = irq_save();
    bool found = key_scanner_next(&scanner, transition);
    irq_restore(primask);
    return found;
}

bool hal_input_waiting(void)
{
    return scanner.count != 0;
}

bool hal_input_next_headset_command(HalHeadsetCommand *command)
{
    uint32_t primask = irq_save();
    bool found = headset_remote_next(&headset.remote, command);
    irq_restore(primask);
    return found;
}

HalHeadsetReading hal_input_headset_reading(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    uint32_t primask = irq_save();
    watched_scans = WATCHED_SCANS;
    HalHeadsetReading reading = headset_monitor_reading(&headset, now_ms);
    irq_restore(primask);
    return reading;
}

CORE_SEAM void board_wait_for_power_hold(uint32_t hold_ms)
{
    uint32_t pressed_at_ms = hal_clock_milliseconds();
    bool held = false;
    for (;;) {
        board_display_service();
        uint32_t now_ms = hal_clock_milliseconds();
        bool down = board_power_key_down();
        if (down && !held) {
            pressed_at_ms = now_ms;
        }
        held = down;
        if (held && now_ms - pressed_at_ms >= hold_ms) {
            return;
        }
    }
}

CORE_SEAM bool board_input_take_key_press(void)
{
    HalKeyTransition transition;
    return hal_input_next_transition(&transition) && transition.pressed;
}

CORE_SEAM void board_wait_for_key(void)
{
    HalKeyTransition transition;
    while (hal_input_next_transition(&transition)) {
    }
    for (;;) {
        board_display_service();
        if (board_input_take_key_press()) {
            return;
        }
    }
}
