#include <stdbool.h>
#include <stdint.h>

#include "core/deadline.h"
#include "core/seam.h"
#include "debug_probe.h"
#include "emmc.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"
#include "trial.h"
#include "usb.h"
#include "usb_session.h"

enum {
    RECONNECT_OFF_MS = 1000,
    RECONNECT_WAIT_MS = 6000
};

static UsbShared shared;
static bool charging_host;

bool hal_power_usb_host(void)
{
    if (!usb_vbus_present()) {
        charging_host = false;
    }
    return charging_host;
}

UsbShared *usb_session_shared(void)
{
    return &shared;
}

CORE_SEAM bool usb_session_listen(bool storage)
{
    if (storage) {
        shared.report = (UsbReport){.internal_ready = emmc_start(&shared.internal)};
        shared.high_speed_failed = false;
    }
    shared.link = (UsbLink){.high_speed_allowed = true};
    usb_power_up();
    if (storage) {
        shared.report.after_power_up = usb_probe();
    }
    bool connected = usb_connect(&shared.link);
    if (storage) {
        shared.report.connect_step = shared.link.step;
    }
    if (!connected) {
        usb_power_down();
    }
    if (storage) {
        board_trial_snapshot(0);
    }
    return connected;
}

bool usb_session_host_seen(void)
{
    bool seen = usb_host_reset_pending();
    charging_host |= seen;
    return seen;
}

void usb_session_stop_listening(void)
{
    shared.report.after_wait = usb_probe();
    usb_power_down();
}

const UsbReport *usb_session_report(void)
{
    return &shared.report;
}

HalUsbLast hal_system_usb_last(void)
{
    return shared.last_session;
}

bool hal_system_restart_pending(void)
{
    return shared.flash_rewritten;
}

void hal_system_set_dac_volume(uint8_t level)
{
    shared.dac_volume = level;
    shared.dac_volume_known = true;
}

void usb_session_request_reconnect(bool on)
{
    shared.reconnect.requested = on;
    if (!on) {
        shared.reconnect.forced = false;
    }
}

CORE_SEAM void hal_system_usb_reconnect(void)
{
    usb_session_request_reconnect(true);
}

bool usb_session_take_reconnect(void)
{
    UsbReconnect *connect = &shared.reconnect;
    uint32_t now_ms = hal_clock_milliseconds();
    if (!connect->requested || connect->in_session ||
        !deadline_elapsed(now_ms, connect->ended_ms, RECONNECT_OFF_MS)) {
        return false;
    }
    connect->requested = false;
    connect->forced = true;
    connect->forced_ms = now_ms;
    return true;
}

bool usb_session_connect_requested(void)
{
    return shared.reconnect.forced || shared.reconnect.requested;
}

bool usb_session_forced(void)
{
    const UsbReconnect *connect = &shared.reconnect;
    return connect->forced &&
           !deadline_elapsed(hal_clock_milliseconds(), connect->forced_ms, RECONNECT_WAIT_MS);
}

bool usb_session_reconnect_stopped(void)
{
    bool forced = shared.reconnect.forced;
    shared.reconnect.forced = false;
    return forced;
}

void usb_session_reconnect_started(void)
{
    shared.reconnect.requested = false;
    shared.reconnect.in_session = true;
    shared.show_modes = false;
}

void usb_session_reconnect_ended(void)
{
    shared.reconnect.in_session = false;
    shared.reconnect.forced = false;
    shared.reconnect.ended_ms = hal_clock_milliseconds();
}
