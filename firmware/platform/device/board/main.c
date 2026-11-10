#include <stdbool.h>

#include "app/app.h"
#include "board.h"
#include "debug_probe.h"
#include "device/battery_gauge.h"
#include "device/usb_plug.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"
#include "overlay.h"
#include "storage_reset.h"
#include "update.h"
#include "usb.h"
#include "usb_report.h"
#include "usb_session.h"

enum {
    START_CLOCK_MHZ = 96,
    EMPTY_NOTICE_MS = 2000
};

static UsbPlug usb_plug;
static HalUsbMode usb_mode;
static bool dac_session;

void hal_system_set_usb_mode(HalUsbMode mode)
{
    usb_mode = mode;
}

bool board_usb_listening(void)
{
    return usb_plug.state == USB_PLUG_LISTENING;
}

static void raise_clock_for_usb(void)
{
    if (hal_system_clock_mhz() < START_CLOCK_MHZ) {
        hal_system_set_clock_mhz(START_CLOCK_MHZ);
    }
}

static const UsbOverlayEntry *load_usb_code(void)
{
    app_release_audio();
    const UsbOverlayEntry *entry = board_usb_overlay_load();
    if (entry != NULL && entry->magic == USB_OVERLAY_MAGIC) {
        return entry;
    }
    usb_power_down();
    app_show_notice("USB", hal_system_restart_pending() ? "Restart the player to finish the update"
                                                        : "USB code missing: reinstall the update");
    board_wait_for_key();
    return NULL;
}

static void run_usb_session(void)
{
    board_stall_pause(true);
    raise_clock_for_usb();
    const UsbOverlayEntry *usb = load_usb_code();
    bool storage_changed = usb != NULL && usb->storage(usb_session_shared());
    if (storage_changed || hal_system_restart_pending()) {
        storage_reset();
        app_start();
        app_request_rescan_if_music_changed();
    }
    app_wake();
    board_stall_pause(false);
}

static void run_dac_session(void)
{
    board_stall_pause(true);
    raise_clock_for_usb();
    const UsbOverlayEntry *usb = load_usb_code();
    if (usb != NULL) {
        usb->dac(usb_session_shared());
    }
    app_wake();
    board_stall_pause(false);
}

static void run_session(void)
{
    board_debug_usb_starting(dac_session);
    if (dac_session) {
        run_dac_session();
    } else {
        run_usb_session();
    }
    board_debug_usb_ended();
}

static void stop_listening(void)
{
    if (dac_session) {
        usb_power_down();
    } else {
        usb_session_stop_listening();
    }
}

static void report_failed_start(void)
{
    if (!dac_session) {
        board_stall_pause(true);
        usb_report_show(usb_session_report());
        board_stall_pause(false);
        app_wake();
    }
}

static void start_listening(void)
{
    raise_clock_for_usb();
    dac_session = usb_mode == HAL_USB_MODE_DAC;
    bool listening = dac_session ? usb_dac_session_listen() : usb_session_listen();
    if (!listening) {
        report_failed_start();
    }
    usb_plug_started(&usb_plug, listening, hal_clock_milliseconds());
}

static void serve_host(void)
{
    if (usb_plug.state == USB_PLUG_LISTENING && usb_session_host_seen()) {
        usb_plug_served(&usb_plug);
        run_session();
    }
}

static void check_usb(void)
{
    if (usb_plug.state != USB_PLUG_LISTENING && board_debug_take_reconnect()) {
        usb_plug_retry(&usb_plug);
    }
    bool cable = usb_vbus_present() || board_debug_usb_forced();
    switch (usb_plug_update(&usb_plug, cable, hal_clock_milliseconds())) {
    case USB_PLUG_START:
        start_listening();
        break;
    case USB_PLUG_STOP:
        stop_listening();
        board_debug_listen_stopped();
        break;
    case USB_PLUG_NOTHING:
        serve_host();
        break;
    }
}

static void switch_off_when_empty(uint32_t boot_millivolts)
{
    if (!battery_boot_cutoff(boot_millivolts, usb_vbus_present())) {
        return;
    }
    app_show_notice("Battery low", "Charge the player to turn it on");
    board_delay_ms(EMPTY_NOTICE_MS);
    hal_power_off();
}

#ifdef ECHO_BOOT_TRACE
enum {
    BOOT_TRACE_MS = 1500
};

static void boot_step(const char *step)
{
    board_backlight_full();
    app_show_notice("Boot", step);
    board_delay_ms(BOOT_TRACE_MS);
}
#else
static void boot_step(const char *step)
{
    (void)step;
}
#endif

static uint32_t idle_us;

uint32_t hal_system_idle_microseconds(void)
{
    return idle_us;
}

static void sleep_until_interrupt(void)
{
    uint32_t started_us = hal_clock_microseconds();
    board_profile_wait();
    idle_us += hal_clock_microseconds() - started_us;
}

void board_main(void)
{
    board_start_record_boot();
    board_debug_boot();
    usb_start_idle();
    board_loader_flag_clear();
    board_power_hold();
    board_power_pair_set(true);
    board_bluetooth_off();
    board_clock_record_boot();
    board_clock_start();
    usb_plug_update(&usb_plug, usb_vbus_present(), hal_clock_milliseconds());
    board_stall_pause(true);
    hal_system_set_clock_mhz(START_CLOCK_MHZ);
    board_watchdog_start();
    uint16_t boot_reading = board_battery_boot_reading();
    board_debug_battery_reading(boot_reading);
    uint32_t boot_millivolts = battery_millivolts(boot_reading);
    board_input_start();
    board_display_start();
    boot_step("display");
    board_glyphs_start();
    boot_step("glyphs");
    update_check_loader_request();
    boot_step("loader request");
    board_jack_poll();
    boot_step("jack");
    switch_off_when_empty(boot_millivolts);
    boot_step("app start");
    app_start();
    boot_step("splash");
    app_show_splash();
    boot_step("main loop");
    board_debug_after_start();
    board_stall_pause(false);
    for (;;) {
        board_stall_beat();
        app_step();
        board_display_service();
        check_usb();
        sleep_until_interrupt();
    }
}
