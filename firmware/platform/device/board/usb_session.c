#include "usb_session.h"
#include "core/seam.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "debug_probe.h"
#include "device/block_device.h"
#include "device/usb_activity.h"
#include "device/usb_fallback.h"
#include "device/usb_leave.h"
#include "emmc.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "hal/system.h"
#include "screens/usb_scene.h"
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
    WORKING_COMMANDS = 2,
    MOTION_FRAME_MS = 400,
    FOLLOW_MS = 10,
    MOTION_STEP = 12
};

static UsbShared *shared;
static bool card_present;
static UsbPort port;
static Msc msc;
static UsbFallback fallback;
static UsbForced forced;
static uint32_t write_buffer[WRITE_BUFFER_SECTORS * MSC_SECTOR_SIZE / 4];
static UsbActivity activity;
static UsbLeave leave;
static UsbScene scene;
static uint32_t refused_before;
static uint32_t motion_ms;

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
    return port.configured && msc.stats.commands >= WORKING_COMMANDS;
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

static const char *storage_detail(void)
{
    if (scene.state == USB_SCENE_WAITING) {
        return i18n_text(scene.holding ? STR_USB_KEEP_HOLDING : STR_USB_WAITING_HINT);
    }
    if (scene.state == USB_SCENE_LEAVING) {
        return i18n_text(STR_USB_LEAVING_HINT);
    }
    return i18n_text(STR_USB_EJECT_DRIVE);
}

static AppUsbStatus storage_status(void)
{
    return (AppUsbStatus){
        .view = APP_USB_STORAGE,
        .information = i18n_text(card_present ? STR_POWER_INTERNAL_CARD : STR_POWER_INTERNAL),
        .detail = storage_detail(),
        .scene = &scene};
}

static void show_storage_state(void)
{
    app_show_usb_status(storage_status());
}

static uint8_t drive_state(uint8_t lun)
{
    if ((msc.present_at_start & (1u << lun)) == 0) {
        return USB_SCENE_DRIVE_ABSENT;
    }
    return msc_ejected(&msc, lun) ? USB_SCENE_DRIVE_EJECTED : USB_SCENE_DRIVE_MOUNTED;
}

static uint8_t traffic_state(UsbLeaveState state, uint32_t now_ms)
{
    if (state == USB_LEAVE_WAITING) {
        return USB_SCENE_WAITING;
    }
    if (state != USB_LEAVE_SERVING) {
        return USB_SCENE_LEAVING;
    }
    UsbActivityKind kind = usb_activity_now(&activity, now_ms);
    if (kind == USB_ACTIVITY_WRITING) {
        return USB_SCENE_WRITING;
    }
    return kind == USB_ACTIVITY_READING ? USB_SCENE_READING : USB_SCENE_CONNECTED;
}

static bool scene_follow(UsbLeaveState state, uint32_t now_ms)
{
    UsbScene before = scene;
    scene.state = traffic_state(state, now_ms);
    scene.holding = usb_leave_holding(&leave);
    scene.high_speed = port.device.speed == USB_SPEED_HIGH;
    for (uint8_t lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        scene.drives[lun] = (UsbSceneDrive){msc.luns[lun].sector_count, drive_state(lun)};
    }
    return before.state != scene.state || before.holding != scene.holding ||
           before.high_speed != scene.high_speed ||
           before.drives[0].state != scene.drives[0].state ||
           before.drives[1].state != scene.drives[1].state;
}

static bool moving(void)
{
    return scene.state == USB_SCENE_READING || scene.state == USB_SCENE_WRITING ||
           scene.state == USB_SCENE_WAITING;
}

static void step_motion(uint32_t now_ms)
{
    if (!moving() || now_ms - motion_ms < MOTION_FRAME_MS) {
        return;
    }
    motion_ms = now_ms;
    scene.turn = (uint8_t)(scene.turn + MOTION_STEP);
    AppUsbStatus status = storage_status();
    app_show_usb_area(status, usb_screen_layout(app_theme(), status).figure);
}

static void follow_keys(void)
{
    HalKeyTransition key;
    while (hal_input_next_transition(&key)) {
        if (key.key == KEY_BACK) {
            usb_leave_key(&leave, key.pressed, key.time_ms);
        }
    }
}

static UsbLeaveState follow_leave(uint32_t now_ms)
{
    follow_keys();
    MscStats stats = msc_stats(&msc);
    usb_activity_follow(&activity, &stats, now_ms);
    UsbLeaveNow now = {.now_ms = now_ms,
                       .quiet = usb_activity_quiet(&activity, now_ms),
                       .refused = stats.writes_after_eject != refused_before};
    UsbLeaveState before = (UsbLeaveState)leave.state;
    UsbLeaveState state = usb_leave_step(&leave, now);
    if (before != USB_LEAVE_REMOVING && state == USB_LEAVE_REMOVING) {
        msc_remove_media(&msc);
        refused_before = stats.writes_after_eject;
    }
    return state;
}

bool usb_session_present(UsbShared *state, UsbForced *connection, uint32_t commands)
{
    return usb_forced_connected(connection, (UsbForcedNow){.cable = usb_vbus_present(),
                                                           .requested = state->reconnect.forced,
                                                           .commands = commands,
                                                           .now_ms = hal_clock_milliseconds()});
}

static void refresh(MscStats *shown, HalBattery *shown_battery)
{
    HalBattery battery = hal_power_battery();
    MscStats stats = msc_stats(&msc);
    uint32_t written = usb_activity_written_kib(&activity);
    if (stats_changed(stats, *shown)) {
        show_counters(stats);
    } else if (app_usb_battery_changed(*shown_battery, battery)) {
        scene.written_kib = written;
        show_storage_state();
    } else if (written != scene.written_kib) {
        scene.written_kib = written;
        AppUsbStatus status = storage_status();
        UsbScreenLayout layout = usb_screen_layout(app_theme(), status);
        app_show_usb_area(status, usb_screen_column(&layout));
    }
    *shown = stats;
    *shown_battery = battery;
}

static UsbSessionEnd ended(UsbLeaveState state)
{
    if (state == USB_LEAVE_FORCED) {
        return USB_SESSION_STOPPED;
    }
    if (state == USB_LEAVE_EJECTED || msc_finished(&msc)) {
        finish_last_reply();
        return USB_SESSION_EJECTED;
    }
    return USB_SESSION_NO_HOST;
}

typedef struct SessionWatch {
    MscStats shown;
    HalBattery battery;
    uint32_t refreshed;
    uint32_t followed;
    UsbLeaveState state;
} SessionWatch;

static void watch_session(SessionWatch *watch, uint32_t now)
{
    watch->followed = now;
    watch->state = follow_leave(now);
    if (scene_follow(watch->state, now)) {
        show_storage_state();
    }
    if (now - watch->refreshed >= COUNTER_REFRESH_MS) {
        refresh(&watch->shown, &watch->battery);
        watch->refreshed = hal_clock_milliseconds();
    }
    step_motion(now);
}

static UsbSessionEnd serve(void)
{
    uint32_t start = hal_clock_milliseconds();
    SessionWatch watch = {msc_stats(&msc), hal_power_battery(), start, start - FOLLOW_MS,
                          USB_LEAVE_SERVING};
    while (usb_session_present(shared, &forced, msc.stats.commands)) {
        uint32_t now = hal_clock_milliseconds();
        if (now - watch.followed >= FOLLOW_MS) {
            watch_session(&watch, now);
        }
        usb_poll(&port);
        board_display_service();
        if (usb_fallback_due(&fallback, session_works(), now)) {
            fall_back_to_full_speed();
        }
        UsbSessionEnd end = ended(watch.state);
        if (end != USB_SESSION_NO_HOST) {
            return end;
        }
    }
    return USB_SESSION_UNPLUGGED;
}

static HalUsbEnd hal_end(UsbSessionEnd end)
{
    if (end == USB_SESSION_EJECTED) {
        return HAL_USB_END_EJECTED;
    }
    bool unplugged = end == USB_SESSION_UNPLUGGED || end == USB_SESSION_STOPPED;
    return unplugged ? HAL_USB_END_UNPLUGGED : HAL_USB_END_NO_HOST;
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
        (AppUsbStatus){.view = APP_USB_CONNECTING, .detail = i18n_text(STR_PLUG_COMPUTER_LINE)});
    fallback = (UsbFallback){.watching = false};
    forced = (UsbForced){0};
    leave = (UsbLeave){0};
    scene = (UsbScene){.state = USB_SCENE_CONNECTED};
    refused_before = 0;
    debug_report_write(shared, DEBUG_REPORT_USB_SESSION);
    prepare_port();
    prepare_luns();
    usb_silence_audio();
    UsbSessionEnd end = USB_SESSION_NO_HOST;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        usb_fallback_start(&fallback, port.link.high_speed_allowed, hal_clock_milliseconds());
        usb_activity_start(&activity, hal_clock_milliseconds());
        scene_follow(USB_LEAVE_SERVING, hal_clock_milliseconds());
        show_storage_state();
        end = serve();
    }
    usb_session_stop_listening();
    remember_session(end);
    shared->show_modes = end == USB_SESSION_EJECTED || end == USB_SESSION_STOPPED;
    return end;
}

CORE_SEAM bool usb_session_storage_changed(void)
{
    MscStats stats = msc_stats(&msc);
    return stats.lun_sectors_written[INTERNAL_LUN] > 0 || stats.lun_sectors_written[CARD_LUN] > 0;
}

static void warn(uint8_t state, StringId hint)
{
    scene.state = state;
    app_show_usb_status((AppUsbStatus){.view = APP_USB_STORAGE,
                                       .information = i18n_text(STR_USB_UNPLUGGED),
                                       .detail = i18n_text(hint),
                                       .scene = &scene});
    board_wait_for_key();
}

static bool left_early(UsbSessionEnd end)
{
    return end == USB_SESSION_STOPPED ||
           (end == USB_SESSION_UNPLUGGED && shared->last_session.left_mounted != 0);
}

CORE_SEAM bool usb_storage_session(UsbShared *state)
{
    shared = state;
    UsbSessionEnd end = run();
    if (left_early(end)) {
        bool stopped = end == USB_SESSION_STOPPED;
        warn(stopped ? USB_SCENE_STOPPED : USB_SCENE_UNPLUGGED,
             stopped ? STR_USB_STOPPED_HINT : STR_USB_UNPLUGGED_HINT);
    }
    bool finished =
        end == USB_SESSION_EJECTED || end == USB_SESSION_UNPLUGGED || end == USB_SESSION_STOPPED;
    if (finished) {
        time_file_take(end == USB_SESSION_EJECTED);
    }
#ifndef ECHO_BOOT_TRACE
    if (finished) {
        update_after_usb(shared);
    }
#endif
    bool script_taken = end == USB_SESSION_EJECTED && debug_script_take();
    return usb_session_storage_changed() || script_taken;
}
