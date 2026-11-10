#include <stdbool.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "board.h"
#include "debug_probe.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "usb.h"

enum {
    POWER_POLL_MS = 250,
    MILLISECONDS_PER_MINUTE = 60000
};

typedef struct PowerWatch {
    volatile bool due;
    bool polled;
    bool sampled;
    uint8_t levels;
    uint32_t polled_ms;
    uint32_t sampled_ms;
} PowerWatch;

static DebugReportAudio audio;
static PowerHistory history;
static PowerWatch watch;
static BoardBatteryReading battery;

void board_debug_battery_reading(uint16_t raw)
{
    battery = (BoardBatteryReading){.known = true, .raw = raw};
}

BoardBatteryReading board_debug_battery_raw(void)
{
    return battery;
}

const PowerHistory *board_debug_power_history(void)
{
    return &history;
}

static uint8_t power_levels(BoardChargePin pin)
{
    return (uint8_t)((usb_vbus_present() ? POWER_SAMPLE_VBUS : 0) |
                     (pin.high ? POWER_SAMPLE_CHARGE_PIN : 0));
}

static void take_sample(uint32_t now_ms)
{
    BatteryGauge gauge = board_battery_gauge();
    BatteryDisplay shown = board_power_shown();
    if (!battery_gauge_known(&gauge) || !shown.shown_known) {
        return;
    }
    uint8_t playing = debug_loop_playing() ? POWER_SAMPLE_PLAYING : 0;
    power_history_record(&history,
                         (PowerSample){.minute = (uint16_t)(now_ms / MILLISECONDS_PER_MINUTE),
                                       .millivolts = (uint16_t)battery_gauge_millivolts(&gauge),
                                       .percent = shown.shown,
                                       .flags = (uint8_t)(watch.levels | playing)});
    watch.sampled = true;
    watch.sampled_ms = now_ms;
}

static void follow_levels(uint8_t levels)
{
    if (!watch.polled || levels != watch.levels) {
        debug_loop_event(DEBUG_EVENT_POWER, levels);
    }
    watch.levels = levels;
}

void board_debug_power_tick(uint32_t now_ms)
{
    if (now_ms - watch.polled_ms >= POWER_POLL_MS) {
        watch.due = true;
    }
}

void board_debug_power_poll(void)
{
    if (!watch.due) {
        return;
    }
    BoardChargePin pin = board_power_charge_pin();
    if (!pin.ready) {
        return;
    }
    uint32_t now_ms = hal_clock_milliseconds();
    watch.due = false;
    follow_levels(power_levels(pin));
    watch.polled = true;
    watch.polled_ms = now_ms;
    if (!watch.sampled || now_ms - watch.sampled_ms >= MILLISECONDS_PER_MINUTE) {
        take_sample(now_ms);
    }
}

void board_debug_usb_starting(bool dac)
{
    audio.state = hal_audio_status().state;
    audio.stats = hal_audio_stats();
    audio.running = audio.stats.elapsed_us != 0;
    debug_loop_event(DEBUG_EVENT_USB_START, dac);
}

void board_debug_usb_ended(void)
{
    debug_loop_event(DEBUG_EVENT_USB_END, (uint32_t)hal_system_usb_last().end);
}

const DebugReportAudio *board_debug_audio(void)
{
    return &audio;
}
