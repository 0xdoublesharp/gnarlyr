#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "board.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "overlay.h"
#include "usb.h"
#include "usb_registers.h"
#include "usb_session.h"

#define SCRIPT_MARK_MAGIC 0x44535352u

enum {
    POWER_POLL_MS = 250,
    MILLISECONDS_PER_MINUTE = 60000,
    RECONNECT_OFF_MS = 1000
};

typedef struct ScriptMark {
    uint32_t magic;
    uint32_t line;
} ScriptMark;

typedef struct Reconnect {
    bool requested;
    bool forced;
    bool in_session;
    uint32_t ended_ms;
    uint32_t interrupted_line;
    DebugReportLoop loop;
} Reconnect;

static const char RESTART_DONE[] = DEBUG_RESTART_DONE;

typedef struct PowerWatch {
    volatile bool due;
    bool polled;
    bool sampled;
    uint8_t levels;
    uint32_t polled_ms;
    uint32_t sampled_ms;
} PowerWatch;

__attribute__((section(".noinit"))) static ScriptMark script_mark;
static Reconnect reconnect;
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

void board_debug_boot(void)
{
    if (script_mark.magic == SCRIPT_MARK_MAGIC) {
        reconnect.interrupted_line = script_mark.line;
    }
    script_mark = (ScriptMark){SCRIPT_MARK_MAGIC, 0};
}

uint32_t board_debug_interrupted_line(void)
{
    return reconnect.interrupted_line;
}

void hal_system_script_mark(uint32_t line)
{
    script_mark.line = line;
}

CORE_SEAM void hal_system_usb_reconnect(void)
{
    reconnect.requested = true;
}

static void note_connect(DebugLoopConnect result)
{
    reconnect.loop.last_connect = (uint8_t)result;
    debug_loop_event(DEBUG_EVENT_USB_CONNECT, result);
}

bool board_debug_take_reconnect(void)
{
    if (!reconnect.requested || reconnect.in_session ||
        hal_clock_milliseconds() - reconnect.ended_ms < RECONNECT_OFF_MS) {
        return false;
    }
    reconnect.requested = false;
    reconnect.forced = true;
    reconnect.loop.attempts++;
    reconnect.loop.cable_at_connect = usb_vbus_present();
    note_connect(DEBUG_LOOP_CONNECT_WAITING);
    return true;
}

bool board_debug_usb_forced(void)
{
    return reconnect.forced;
}

DebugReportLoop board_debug_loop(void)
{
    return reconnect.loop;
}

void board_debug_after_start(void)
{
    char text[sizeof RESTART_DONE - 1];
    if (!hal_internal_file_read(DEBUG_RESTART_PATH, text, sizeof text)) {
        return;
    }
    for (size_t index = 0; index < sizeof text; index++) {
        if (text[index] != RESTART_DONE[index]) {
            return;
        }
        text[index] = 0;
    }
    hal_internal_file_write(DEBUG_RESTART_PATH, text, sizeof text);
    reconnect.loop.restarted = true;
    reconnect.requested = true;
}

static void note_audio(void)
{
    audio.state = hal_audio_status().state;
    audio.stats = hal_audio_stats();
    audio.running = audio.stats.elapsed_us != 0;
}

void hal_system_save_report(uint32_t reason)
{
    if (reconnect.in_session || hal_system_restart_pending()) {
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
    if (reconnect.in_session || board_usb_listening()) {
        return;
    }
    if (kept_on && !usb_powered) {
        usb_power_up();
        set_bits(DCTL, DCTL_SOFT_DISCONNECT);
    } else if (!kept_on && usb_powered) {
        usb_power_down();
    }
}

void board_debug_listen_timed_out(void)
{
    if (!reconnect.forced) {
        return;
    }
    reconnect.forced = false;
    reconnect.loop.unanswered++;
    note_connect(DEBUG_LOOP_CONNECT_NO_HOST);
    hal_system_save_report(DEBUG_REPORT_NO_HOST);
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
    debug_runner_abort();
    reconnect.requested = false;
    reconnect.in_session = true;
    if (reconnect.forced) {
        reconnect.loop.answered++;
        note_connect(DEBUG_LOOP_CONNECT_ANSWERED);
    }
    note_audio();
    debug_loop_event(DEBUG_EVENT_USB_START, dac);
}

void board_debug_usb_ended(void)
{
    reconnect.in_session = false;
    reconnect.forced = false;
    reconnect.ended_ms = hal_clock_milliseconds();
    debug_loop_event(DEBUG_EVENT_USB_END, (uint32_t)hal_system_usb_last().end);
}

const DebugReportAudio *board_debug_audio(void)
{
    return &audio;
}
