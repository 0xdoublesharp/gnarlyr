#include <stdbool.h>
#include <stdint.h>

#include "core/seam.h"
#include "debug_probe.h"
#include "emmc.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"
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

CORE_SEAM bool usb_session_listen(void)
{
    shared.report = (UsbReport){.internal_ready = emmc_start(&shared.internal)};
    shared.link = (UsbLink){.high_speed_allowed = !shared.high_speed_failed};
    usb_power_up();
    shared.report.after_power_up = usb_probe();
    bool connected = usb_connect(&shared.link);
    shared.report.connect_step = shared.link.step;
    if (!connected) {
        usb_power_down();
    }
    return connected;
}

bool usb_dac_session_listen(void)
{
    shared.link = (UsbLink){.high_speed_allowed = true};
    usb_power_up();
    if (usb_connect(&shared.link)) {
        return true;
    }
    usb_power_down();
    return false;
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
    shared.reconnect.manual = false;
    if (!on) {
        shared.reconnect.forced = false;
    }
}

CORE_SEAM void hal_system_usb_reconnect(void)
{
    usb_session_request_reconnect(true);
}

void hal_system_usb_connect(bool on)
{
    UsbReconnect *connect = &shared.reconnect;
    if (on == connect->manual) {
        return;
    }
    connect->manual = on;
    connect->requested = on;
    if (!on && connect->forced && !connect->in_session) {
        connect->forced = false;
#ifdef ECHO_DEBUG_LOOP
        board_debug_connect_cancelled();
#endif
    }
}

bool usb_session_take_reconnect(void)
{
    UsbReconnect *connect = &shared.reconnect;
    uint32_t now_ms = hal_clock_milliseconds();
    if (!connect->requested || connect->in_session ||
        now_ms - connect->ended_ms < RECONNECT_OFF_MS) {
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
           (connect->manual || hal_clock_milliseconds() - connect->forced_ms < RECONNECT_WAIT_MS);
}

bool usb_session_reconnect_stopped(void)
{
    bool forced = shared.reconnect.forced;
    shared.reconnect.forced = false;
    shared.reconnect.manual = false;
    return forced;
}

void usb_session_reconnect_started(void)
{
    shared.reconnect.requested = false;
    shared.reconnect.manual = false;
    shared.reconnect.in_session = true;
    shared.show_modes = false;
}

void usb_session_reconnect_ended(void)
{
    shared.reconnect.in_session = false;
    shared.reconnect.forced = false;
    shared.reconnect.ended_ms = hal_clock_milliseconds();
}
