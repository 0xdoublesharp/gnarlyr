#include <stddef.h>
#include <stdint.h>

#include "app/app.h"
#include "app/debug_loop.h"
#include "board.h"
#include "core/i18n.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "device/usb_forced.h"
#include "emmc.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "internal_volume.h"

static const char RUN_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/debug/run.txt";
static const char RESTART_PATH[] = DEBUG_RESTART_PATH;
static const char RESTART_TEXT[] = DEBUG_RESTART_REQUEST;
static const char RESTART_DONE[] = DEBUG_RESTART_DONE;

_Static_assert(sizeof RESTART_DONE == sizeof RESTART_TEXT, "the answer overwrites the request");

static UsbForced forced;

bool debug_session_cable(bool vbus, const Msc *msc)
{
    return usb_forced_connected(&forced, (UsbForcedNow){.cable = vbus,
                                                        .requested = board_debug_usb_forced(),
                                                        .commands = msc_stats(msc).commands,
                                                        .now_ms = hal_clock_milliseconds()});
}

static bool take_script(FatVolume *volume)
{
    FatFile file;
    if (!fat_open(volume, RUN_PATH, &file)) {
        return false;
    }
    DebugScriptRun *run = debug_runner_script();
    DebugScriptHost host = {hal_system_version(), hal_system_restart_pending()};
    uint32_t length = file.size < DEBUG_SCRIPT_BYTES ? file.size : DEBUG_SCRIPT_BYTES;
    bool read = fat_read(volume, &file, 0, run->text, length);
    fat_delete(volume, RUN_PATH);
    debug_script_load(run, read ? file.size : 0, host);
    debug_runner_begin();
    return true;
}

CORE_SEAM bool debug_script_take(void)
{
    EmmcInfo info;
    FatVolume *volume = emmc_start(&info) ? internal_volume_remount() : NULL;
    if (volume == NULL) {
        return false;
    }
    bool stale_request = fat_delete(volume, RESTART_PATH);
    return take_script(volume) || stale_request;
}

static bool answer_restart_request(FatVolume *volume)
{
    char text[sizeof RESTART_TEXT - 1];
    FatFile file;
    if (!fat_open(volume, RESTART_PATH, &file) || file.size < sizeof text ||
        !fat_read(volume, &file, 0, text, sizeof text)) {
        return false;
    }
    for (size_t index = 0; index < sizeof text; index++) {
        if (text[index] != RESTART_TEXT[index]) {
            return false;
        }
    }
    return fat_write(volume, &file, 0, RESTART_DONE, sizeof text);
}

CORE_SEAM void debug_restart_after_install(FatVolume *volume, const UsbShared *shared)
{
    if (!answer_restart_request(volume)) {
        return;
    }
    debug_report_write(shared, DEBUG_REPORT_RESTART);
    app_show_notice(i18n_text(STR_UPDATE_INSTALLED), i18n_text(STR_UPDATE_RESTARTING));
    board_display_service();
    board_restart();
}
