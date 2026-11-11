#include "usb_session.h"
#include "core/seam.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "debug_probe.h"
#include "device/block_device.h"
#include "device/usb_fallback.h"
#include "emmc.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "sd.h"
#include "usb.h"
#include "usb_report.h"

enum {
    HOST_TIMEOUT_MS = 3000,
    LAST_REPLY_TIMEOUT_MS = 200,
    COUNTER_REFRESH_MS = 1000,
    COUNTER_TEXT_CAPACITY = 48,
    SERIAL_DIGITS = 8,
    INTERNAL_LUN = 0,
    CARD_LUN = 1,
    LUN_COUNT = 2,
    WRITE_BUFFER_SECTORS = 64,
    WORKING_COMMANDS = 2
};

static UsbShared *shared;
static bool card_present;
static UsbPort port;
static Msc msc;
static UsbFallback fallback;
static UsbForced forced;
static uint32_t write_buffer[WRITE_BUFFER_SECTORS * MSC_SECTOR_SIZE / 4];

uint32_t *usb_session_buffer(void)
{
    return write_buffer;
}

static void serial_text(uint32_t serial, char *text)
{
    static const char DIGITS[] = "0123456789ABCDEF";
    for (int index = 0; index < SERIAL_DIGITS; index++) {
        text[index] = DIGITS[(serial >> (28 - 4 * index)) & 0xFu];
    }
    text[SERIAL_DIGITS] = '\0';
}

static void prepare_port(void)
{
    char serial[SERIAL_DIGITS + 1];
    serial_text(shared->internal.serial, serial);
    port = (UsbPort){.link = shared->link, .msc = &msc, .bulk_packet = USB_PACKET_SIZE};
    usb_device_init(&port.device, serial, LUN_COUNT);
    usb_device_allow_high_speed(&port.device, port.link.high_speed_allowed);
}

static void prepare_luns(void)
{
    BlockDevice card;
    bool card_ready = sd_start(&card);
    MscLun luns[LUN_COUNT] = {
        {emmc_volume_read, emmc_volume_write, NULL, shared->internal.layout.volume_sectors, true,
         shared->report.internal_ready, "Internal storage"},
        {sd_read, sd_write, NULL, sd_sector_count(), true, card_ready, "SD card"},
    };
    card_present = card_ready;
    msc_init(&msc, luns, LUN_COUNT);
    msc_use_write_buffer(&msc, write_buffer, WRITE_BUFFER_SECTORS);
}

static void finish_last_reply(void)
{
    uint32_t start = hal_clock_milliseconds();
    while (usb_reply_pending(&port) && hal_clock_milliseconds() - start < LAST_REPLY_TIMEOUT_MS) {
        usb_poll(&port);
    }
}

static void append_count(StringBuilder *builder, char label, uint32_t value)
{
    string_builder_append_char(builder, label);
    string_builder_append_unsigned(builder, value);
    string_builder_append_char(builder, ' ');
}

static void append_history(StringBuilder *line)
{
    MscHistory history = msc_history(&msc);
    for (uint32_t index = 0; index < history.count; index++) {
        MscHistoryEntry entry = history.entries[index];
        usb_report_append_hex(line, entry.opcode, 2);
        string_builder_append_unsigned(line, entry.status);
        string_builder_append_char(line, entry.lun == 0 ? ' ' : '+');
    }
}

static void show_counters(MscStats stats)
{
    char title[COUNTER_TEXT_CAPACITY];
    char message[COUNTER_TEXT_CAPACITY];
    StringBuilder top = string_builder_make(title, sizeof title);
    string_builder_append(&top, "USB ");
    append_count(&top, 'R', stats.sectors_read);
    append_count(&top, 'W', stats.sectors_written);
    append_count(&top, 'E', stats.read_failures + stats.write_failures);
    StringBuilder line = string_builder_make(message, sizeof message);
    append_history(&line);
    app_show_notice(title, message);
}

static bool stats_changed(MscStats now, MscStats shown)
{
    return now.write_failures != shown.write_failures || now.read_failures != shown.read_failures;
}

static bool session_works(void)
{
    return port.configured && msc_stats(&msc).commands >= WORKING_COMMANDS;
}

static void fall_back_to_full_speed(void)
{
    shared->high_speed_failed = true;
    port.link.high_speed_allowed = false;
    port.configured = false;
    port.in_busy = false;
    usb_device_allow_high_speed(&port.device, false);
    msc_reset(&msc);
    usb_port_connect(&port);
}

static void show_storage_state(void)
{
    app_show_usb_status((AppUsbStatus){
        .view = APP_USB_STORAGE,
        .information = i18n_text(card_present ? STR_POWER_INTERNAL_CARD : STR_POWER_INTERNAL),
        .detail = i18n_text(card_present ? STR_USB_EJECT_DRIVES : STR_USB_EJECT_DRIVE)});
}

bool usb_session_present(UsbShared *state, UsbForced *connection, uint32_t commands)
{
    return usb_forced_connected(connection, (UsbForcedNow){.cable = usb_vbus_present(),
                                                           .requested = state->reconnect.forced,
                                                           .commands = commands,
                                                           .now_ms = hal_clock_milliseconds()});
}

static UsbSessionEnd serve(void)
{
    MscStats shown = msc_stats(&msc);
    HalBattery shown_battery = hal_power_battery();
    uint32_t refreshed = hal_clock_milliseconds();
    while (usb_session_present(shared, &forced, msc_stats(&msc).commands)) {
        uint32_t now = hal_clock_milliseconds();
        if (now - refreshed >= COUNTER_REFRESH_MS) {
            HalBattery battery = hal_power_battery();
            MscStats stats = msc_stats(&msc);
            if (stats_changed(stats, shown)) {
                show_counters(stats);
            } else if (app_usb_battery_changed(shown_battery, battery)) {
                show_storage_state();
            }
            shown = stats;
            shown_battery = battery;
            refreshed = hal_clock_milliseconds();
        }
        usb_poll(&port);
        board_display_service();
        if (usb_fallback_due(&fallback, session_works(), hal_clock_milliseconds())) {
            fall_back_to_full_speed();
        }
        if (msc_finished(&msc)) {
            finish_last_reply();
            return USB_SESSION_EJECTED;
        }
    }
    return USB_SESSION_UNPLUGGED;
}

static HalUsbEnd hal_end(UsbSessionEnd end)
{
    if (end == USB_SESSION_EJECTED) {
        return HAL_USB_END_EJECTED;
    }
    return end == USB_SESSION_UNPLUGGED ? HAL_USB_END_UNPLUGGED : HAL_USB_END_NO_HOST;
}

static void remember_session(UsbSessionEnd end)
{
    MscStats stats = msc_stats(&msc);
    shared->last_session = (HalUsbLast){.end = hal_end(end),
                                        .left_mounted = msc_left_mounted(&msc),
                                        .internal_written = stats.lun_sectors_written[INTERNAL_LUN],
                                        .card_written = stats.lun_sectors_written[CARD_LUN],
                                        .writes_after_eject = stats.writes_after_eject};
}

static UsbSessionEnd run(void)
{
    app_show_usb_status(
        (AppUsbStatus){.view = APP_USB_CONNECTING, .detail = i18n_text(STR_USB_STARTING)});
    fallback = (UsbFallback){.watching = false};
    forced = (UsbForced){0};
    debug_report_write(shared, DEBUG_REPORT_USB_SESSION);
    prepare_port();
    prepare_luns();
    usb_silence_audio();
    UsbSessionEnd end = USB_SESSION_NO_HOST;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        usb_fallback_start(&fallback, port.link.high_speed_allowed, hal_clock_milliseconds());
        show_storage_state();
        end = serve();
    }
    usb_session_stop_listening();
    remember_session(end);
    shared->show_modes = end == USB_SESSION_EJECTED;
    return end;
}

CORE_SEAM bool usb_session_storage_changed(void)
{
    MscStats stats = msc_stats(&msc);
    return stats.lun_sectors_written[INTERNAL_LUN] > 0 || stats.lun_sectors_written[CARD_LUN] > 0;
}

CORE_SEAM bool usb_storage_session(UsbShared *state)
{
    shared = state;
    UsbSessionEnd end = run();
    if (end == USB_SESSION_UNPLUGGED && shared->last_session.left_mounted != 0) {
        app_show_notice(i18n_text(STR_USB_UNPLUGGED), i18n_text(STR_USB_UNPLUGGED_HINT));
        board_wait_for_key();
    }
#ifndef ECHO_BOOT_TRACE
    if (end == USB_SESSION_EJECTED || end == USB_SESSION_UNPLUGGED) {
        update_after_usb(shared);
    }
#endif
    bool script_taken = end == USB_SESSION_EJECTED && debug_script_take();
    return usb_session_storage_changed() || script_taken;
}
