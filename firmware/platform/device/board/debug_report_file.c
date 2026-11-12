#include <stdint.h>

#include "app/debug_loop.h"
#include "board.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "usb.h"
#include "usb_registers.h"
#include "usb_session.h"

enum {
    SLOT_DIGIT = sizeof "/" FIRMWARE_DATA_DIRECTORY "/debug/report" - 1
};

static const char REPORT_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/debug/report0.txt";

typedef struct ReportPath {
    char text[sizeof REPORT_PATH];
} ReportPath;

static ReportPath slot_path(uint32_t slot)
{
    ReportPath path;
    for (uint32_t index = 0; index < sizeof REPORT_PATH; index++) {
        path.text[index] = REPORT_PATH[index];
    }
    path.text[SLOT_DIGIT] = (char)('0' + slot);
    return path;
}

static DebugReportPlace place_for(char *head, bool cold_start)
{
    DebugReportSlot slots[DEBUG_REPORT_SLOTS];
    for (uint32_t slot = 0; slot < DEBUG_REPORT_SLOTS; slot++) {
        ReportPath path = slot_path(slot);
        bool read = hal_internal_file_read(path.text, head, DEBUG_REPORT_HEAD_BYTES);
        slots[slot] = read ? debug_report_read_head(head, DEBUG_REPORT_HEAD_BYTES)
                           : (DebugReportSlot){.valid = false};
    }
    return debug_report_place(slots, cold_start);
}

static DebugReportPower power_now(bool charging)
{
    BatteryGauge gauge = board_battery_gauge();
    BoardChargePin pin = board_power_charge_pin();
    BoardBatteryReading reading = board_debug_battery_raw();
    BatteryDisplay shown = board_power_shown();
    uint32_t millivolts = battery_gauge_millivolts(&gauge);
    return (DebugReportPower){.vbus = usb_vbus_present(),
                              .usb_powered = board_debug_usb_is_powered(),
                              .usb_kept_on = board_debug_usb_kept_on(),
                              .pin_known = pin.ready,
                              .pin_high = pin.high,
                              .adc_known = reading.known,
                              .adc_from_gauge = gauge.count != 0,
                              .adc_raw = reading.raw,
                              .gauge_readings = gauge.count,
                              .gauge_millivolts = millivolts,
                              .percent_plain = battery_percent_for(millivolts),
                              .percent_gauge = battery_gauge_percent(&gauge, charging && !pin.high),
                              .shown_known = shown.shown_known,
                              .percent_shown = shown.shown,
                              .history = board_debug_power_history()};
}

static DebugReportUsbWords usb_words_now(void)
{
    DebugReportUsbWords words = {.grf_start = board_debug_grf_start(),
                                 .core_known = board_debug_usb_is_powered()};
    board_debug_read_grf(words.grf);
    if (words.core_known) {
        words.gotgctl = read_register(GOTGCTL);
    }
    return words;
}

static DebugReportInput gather(const UsbShared *shared, uint32_t reason)
{
    HalBattery battery = hal_power_battery();
    return (DebugReportInput){.reason = (DebugReportReason)reason,
                              .name = FIRMWARE_NAME,
                              .version = hal_system_version(),
                              .uptime_ms = hal_clock_milliseconds(),
                              .clock_mhz = hal_system_clock_mhz(),
                              .clock_us = hal_clock_microseconds(),
                              .idle_us = hal_system_idle_microseconds(),
                              .battery = battery,
                              .power = power_now(battery.charging),
                              .start = hal_system_start_info(),
                              .usb = shared->last_session,
                              .usb_words = usb_words_now(),
                              .audio = *board_debug_audio(),
                              .bluetooth = &hal_bluetooth_status()->link,
                              .frames = app_debug_frames(),
                              .loop = board_debug_loop(),
                              .script = debug_runner_script(),
                              .interrupted_line = board_debug_interrupted_line(),
                              .log = debug_loop_log(),
                              .profile = board_profile()};
}

static bool reserved(const char *path)
{
    HalReserveResult result = hal_internal_file_reserve(path, DEBUG_REPORT_BYTES);
    return result == HAL_RESERVE_EXISTS || result == HAL_RESERVE_CREATED;
}

CORE_SEAM void debug_report_write(const UsbShared *shared, uint32_t reason)
{
    char *text = (char *)usb_session_buffer();
    DebugReportInput input = gather(shared, reason);
    bool cold_start = debug_report_cold_start(&input.start, input.uptime_ms);
    DebugReportPlace place = place_for(text, cold_start);
    ReportPath path = slot_path(place.slot);
    input.sequence = place.sequence;
    board_profile_hold(true);
    debug_report_format(&input, text, DEBUG_REPORT_BYTES);
    board_profile_hold(false);
    if (reserved(path.text)) {
        hal_internal_file_write(path.text, text, DEBUG_REPORT_BYTES);
    }
}
