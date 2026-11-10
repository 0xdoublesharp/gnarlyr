#include <stdint.h>

#include "app/debug_loop.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "usb_session.h"

static const char REPORT_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/debug/report.txt";

static DebugReportInput gather(const UsbShared *shared)
{
    return (DebugReportInput){.name = FIRMWARE_NAME,
                              .version = hal_system_version(),
                              .uptime_ms = hal_clock_milliseconds(),
                              .clock_mhz = hal_system_clock_mhz(),
                              .clock_us = hal_clock_microseconds(),
                              .idle_us = hal_system_idle_microseconds(),
                              .battery = hal_power_battery(),
                              .start = hal_system_start_info(),
                              .usb = shared->last_session,
                              .audio = *board_debug_audio(),
                              .frames = app_debug_frames(),
                              .log = debug_loop_log(),
                              .profile = board_profile()};
}

static bool reserved(void)
{
    HalReserveResult result = hal_internal_file_reserve(REPORT_PATH, DEBUG_REPORT_BYTES);
    return result == HAL_RESERVE_EXISTS || result == HAL_RESERVE_CREATED;
}

CORE_SEAM void debug_report_write(const UsbShared *shared)
{
    char *text = (char *)usb_session_buffer();
    DebugReportInput input = gather(shared);
    debug_report_format(&input, text, DEBUG_REPORT_BYTES);
    if (reserved()) {
        hal_internal_file_write(REPORT_PATH, text, DEBUG_REPORT_BYTES);
    }
}
