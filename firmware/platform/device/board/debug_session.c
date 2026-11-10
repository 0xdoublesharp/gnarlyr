#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "board.h"
#include "debug_probe.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "overlay.h"
#include "usb.h"
#include "usb_registers.h"
#include "usb_session.h"

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

static bool in_session;
static DebugReportAudio audio;
static PowerHistory history;
static PowerWatch watch;
static BoardBatteryReading battery;
static bool usb_powered;
static bool usb_kept_on;

void board_debug_usb_powered(bool powered)
{
    usb_powered = powered;
}

bool board_debug_usb_is_powered(void)
{
    return usb_powered;
}

bool board_debug_usb_kept_on(void)
{
    return usb_kept_on;
}

HalUsbSense hal_system_usb_sense(void)
{
    return (HalUsbSense){
        .kept_on = usb_kept_on, .cable = usb_vbus_present(), .powered = usb_powered};
}

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
    uint8_t playing = (uint8_t)((debug_loop_playing() ? POWER_SAMPLE_PLAYING : 0) |
                                (usb_powered ? POWER_SAMPLE_USB_POWERED : 0) |
                                (usb_kept_on ? POWER_SAMPLE_USB_KEPT_ON : 0));
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

static void note_audio(void)
{
    audio.state = hal_audio_status().state;
    audio.stats = hal_audio_stats();
    audio.running = audio.stats.elapsed_us != 0;
}

void hal_system_save_report(uint32_t reason)
{
    if (in_session || hal_system_restart_pending()) {
        return;
    }
    board_stall_pause(true);
    note_audio();
    board_audio_shut_down();
    const UsbOverlayEntry *usb = board_usb_overlay_load();
    if (usb != NULL && usb->magic == USB_OVERLAY_MAGIC) {
        usb->report(usb_session_shared(), reason);
    }
    board_stall_pause(false);
}

void hal_system_keep_usb_on(bool kept_on)
{
    usb_kept_on = kept_on;
    if (in_session || board_usb_listening()) {
        return;
    }
    if (kept_on && !usb_powered) {
        usb_power_up();
        set_bits(DCTL, DCTL_SOFT_DISCONNECT);
    } else if (!kept_on && usb_powered) {
        usb_power_down();
    }
}

void board_debug_power_off(void)
{
    uint32_t reason = debug_loop_take_shutdown();
    if (reason != DEBUG_REPORT_NONE) {
        hal_system_save_report(reason);
    }
}

void board_debug_usb_starting(bool dac)
{
    in_session = true;
    note_audio();
    debug_loop_event(DEBUG_EVENT_USB_START, dac);
}

void board_debug_usb_ended(void)
{
    in_session = false;
    debug_loop_event(DEBUG_EVENT_USB_END, (uint32_t)hal_system_usb_last().end);
}

const DebugReportAudio *board_debug_audio(void)
{
    return &audio;
}
