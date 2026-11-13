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
#include "device/volume_space.h"
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
    MOTION_FRAME_MS = 400,
    FOLLOW_MS = 10,
    NUDGE_MS = 1500,
    MOTION_STEP = 12,
    HINT_CAPACITY = 96,
    CAPTURE_STORAGE_SLOT = 0,
    MS_PER_SECOND = 1000,
    US_PER_PERCENT_MS = 10,
    FULL_PERCENT = 100,
    LIGHT_FRAME_MS = 40,
    GLOW_STEP = 32
};

typedef struct TimedDrive {
    bool (*read)(void *context, uint32_t lba, uint32_t count, void *buffer);
    bool (*write)(void *context, uint32_t lba, uint32_t count, const void *buffer);
} TimedDrive;

typedef struct DriveLight {
    uint32_t commands[2];
    uint32_t seen_ms[2];
    bool seen[2];
} DriveLight;

typedef struct SessionClock {
    DriveLight lights[2];
    UsbSteady busy;
    uint32_t traffic_ms;
    uint32_t lit_ms;
    bool traffic_seen;
    uint32_t started_ms;
    uint32_t media_us;
    uint32_t media_seen_us;
    uint64_t busy_us;
    uint32_t back_pressed_ms;
    uint32_t nudged_ms;
    bool moving;
} SessionClock;

static const char CAPTURE_STORAGE[] = "storage";
static const TimedDrive DRIVES[] = {{emmc_volume_read, emmc_volume_write}, {sd_read, sd_write}};

static UsbShared *shared;
static bool card_present;
static bool eject_card;
static UsbPort port;
static Msc msc;
static UsbFallback fallback;
static UsbForced forced;
static uint32_t write_buffer[WRITE_BUFFER_SECTORS * MSC_SECTOR_SIZE / 4];
static SessionClock session_clock;
static char hint[HINT_CAPACITY];

_Static_assert(sizeof write_buffer == USB_SESSION_BUFFER_BYTES,
               "the session buffer size is shared");
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

static bool timed_read(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    uint32_t start = hal_clock_microseconds();
    bool read = DRIVES[(uintptr_t)context].read(NULL, lba, count, buffer);
    session_clock.media_us += hal_clock_microseconds() - start;
    return read;
}

static bool timed_write(void *context, uint32_t lba, uint32_t count, const void *buffer)
{
    uint32_t start = hal_clock_microseconds();
    bool written = DRIVES[(uintptr_t)context].write(NULL, lba, count, buffer);
    session_clock.media_us += hal_clock_microseconds() - start;
    return written;
}

static uint32_t free_at_connect(bool ready, uint8_t lun)
{
    BlockDevice device = {DRIVES[lun].read, NULL, NULL};
    VolumeSpaceBuffer buffer = {write_buffer, WRITE_BUFFER_SECTORS};
    return ready ? volume_space_free_sectors(device, buffer) : USB_SCENE_FREE_UNKNOWN;
}

static void prepare_luns(void)
{
    BlockDevice card;
    bool card_ready = sd_start(&card);
    void *internal = (void *)(uintptr_t)INTERNAL_LUN;
    void *sd = (void *)(uintptr_t)CARD_LUN;
    MscLun luns[LUN_COUNT] = {
        {timed_read, timed_write, internal, shared->internal.layout.volume_sectors, true,
         shared->report.internal_ready, "Internal storage"},
        {timed_read, timed_write, sd, sd_sector_count(), true, card_ready, "SD card"},
    };
    scene.usage[INTERNAL_LUN].free_sectors =
        free_at_connect(shared->report.internal_ready, INTERNAL_LUN);
    scene.usage[CARD_LUN].free_sectors = free_at_connect(card_ready, CARD_LUN);
    card_present = card_ready;
    eject_card = false;
    msc_init(&msc, luns, LUN_COUNT);
    msc_use_write_buffer(&msc, write_buffer, WRITE_BUFFER_SECTORS);
}

static void serve_while_drawing(void)
{
    usb_poll(&port);
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

static HalUsbSetupTrace fallback_setups;

static void fall_back_to_full_speed(void)
{
    fallback_setups = port.setups;
    shared->high_speed_failed = true;
    port.link.high_speed_allowed = false;
    port.configured = false;
    port.in_busy = false;
    usb_device_allow_high_speed(&port.device, false);
    msc_reset(&msc);
    usb_port_connect(&port);
}

static void watch_speed(uint32_t now)
{
    if (port.enumerated) {
        usb_fallback_enumerated(&fallback, now);
    }
    UsbFallbackSigns signs = {port.configured, msc.stats.commands};
    if (usb_fallback_due(&fallback, signs, now)) {
        fall_back_to_full_speed();
    }
}

static StringId menu_action(int pages)
{
    if (scene.page + 1 >= pages) {
        return STR_USB_MENU_OVERVIEW;
    }
    return scene.page == USB_SCENE_OVERVIEW ? STR_USB_MENU_DETAILS : STR_USB_MENU_MORE;
}

static const char *paged_hint(void)
{
    AppUsbStatus probe = {.view = APP_USB_STORAGE, .scene = &scene};
    StringId frame = card_present ? STR_USB_DRIVES_HINT : STR_USB_DRIVE_HINT;
    StringBuilder builder = string_builder_make(hint, sizeof hint);
    i18n_format_text(&builder, frame, i18n_text(STR_USB_MENU_OVERVIEW));
    probe.detail = hint;
    int pages = usb_screen_pages(app_theme(), probe);
    if (pages <= 1) {
        return i18n_text(card_present ? STR_USB_EJECT_DRIVES : STR_USB_EJECT_DRIVE);
    }
    builder = string_builder_make(hint, sizeof hint);
    i18n_format_text(&builder, frame, i18n_text(menu_action(pages)));
    return hint;
}

static const char *storage_detail(void)
{
    if (scene.state == USB_SCENE_WAITING) {
        return i18n_text(scene.holding ? STR_USB_KEEP_HOLDING : STR_USB_WAITING_HINT);
    }
    if (scene.state == USB_SCENE_LEAVING) {
        return i18n_text(STR_USB_LEAVING_HINT);
    }
    return paged_hint();
}

static AppUsbStatus storage_status(void)
{
    return (AppUsbStatus){.view = APP_USB_STORAGE,
                          .information = i18n_text(!shared->report.internal_ready
                                                       ? STR_POWER_INTERNAL_UNAVAILABLE
                                                   : card_present ? STR_POWER_INTERNAL_CARD
                                                                  : STR_POWER_INTERNAL),
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

static bool traffic(uint8_t state)
{
    return state == USB_SCENE_CONNECTED || state == USB_SCENE_READING || state == USB_SCENE_WRITING;
}

static bool same_layout(uint8_t before, uint8_t after)
{
    return before == after || (traffic(before) && traffic(after));
}

static bool scene_follow(UsbLeaveState state, uint32_t now_ms)
{
    UsbScene before = scene;
    scene.state = traffic_state(state, now_ms);
    scene.holding = usb_leave_holding(&leave);
    scene.high_speed = port.device.speed == USB_SPEED_HIGH;
    scene.fell_back = shared->high_speed_failed;
    for (uint8_t lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        scene.drives[lun].sectors = msc.luns[lun].sector_count;
        scene.drives[lun].state = drive_state(lun);
    }
    return !same_layout(before.state, scene.state) ||
           (before.holding != scene.holding && scene.state == USB_SCENE_WAITING) ||
           before.high_speed != scene.high_speed || before.fell_back != scene.fell_back ||
           before.drives[0].state != scene.drives[0].state ||
           before.drives[1].state != scene.drives[1].state;
}

static bool moving(void)
{
    return scene.state == USB_SCENE_WAITING;
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

static void next_page(void)
{
    AppUsbStatus status = storage_status();
    int pages = usb_screen_pages(app_theme(), status);
    scene.page = (uint8_t)((scene.page + 1) % pages);
    shared->usb_page = scene.page;
    show_storage_state();
}

static void show_hint(void)
{
    AppUsbStatus status = storage_status();
    app_show_usb_area(status, usb_screen_layout(app_theme(), status).detail);
}

static void back_tapped(uint32_t now_ms)
{
    if (scene.page != USB_SCENE_OVERVIEW) {
        scene.page = USB_SCENE_OVERVIEW;
        shared->usb_page = scene.page;
        scene.nudge = false;
        show_storage_state();
        return;
    }
    scene.nudge = true;
    session_clock.nudged_ms = now_ms;
    show_hint();
}

static void follow_back(HalKeyTransition key)
{
    bool tapped = !key.pressed && leave.state == USB_LEAVE_SERVING &&
                  key.time_ms - session_clock.back_pressed_ms < USB_LEAVE_HOLD_MS;
    usb_leave_key(&leave, key.pressed, key.time_ms);
    if (key.pressed) {
        session_clock.back_pressed_ms = key.time_ms;
    } else if (tapped && traffic(scene.state)) {
        back_tapped(key.time_ms);
    }
}

static void follow_nudge(uint32_t now_ms)
{
    if (scene.nudge && now_ms - session_clock.nudged_ms >= NUDGE_MS) {
        scene.nudge = false;
        show_hint();
    }
}

static void follow_keys(void)
{
    HalKeyTransition key;
    while (hal_input_next_transition(&key)) {
        if (key.key == KEY_PLAY && key.pressed && card_present) {
            eject_card = true;
        }
        bool serving = leave.state == USB_LEAVE_SERVING && traffic(scene.state);
        if (key.key == KEY_MENU && key.pressed && serving) {
            scene.nudge = false;
            next_page();
        }
        if (key.key == KEY_BACK) {
            follow_back(key);
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
    } else {
        scene.written_kib = written;
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

static bool drives_changed(const MscStats *stats)
{
    bool changed = false;
    for (int lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        MscTraffic traffic = stats->traffic[lun];
        UsbSceneUsage *drive = &scene.usage[lun];
        bool active = traffic.reads + traffic.writes != drive->reads + drive->writes;
        changed = changed || active || drive->active;
        drive->active = active;
        drive->read_sectors = traffic.sectors_read;
        drive->written_sectors = traffic.sectors_written;
        drive->reads = traffic.reads;
        drive->writes = traffic.writes;
    }
    return changed;
}

static void follow_link(const MscStats *stats, uint32_t now_ms)
{
    UsbSceneLink *link = &scene.link;
    session_clock.busy_us += session_clock.media_us - session_clock.media_seen_us;
    session_clock.media_seen_us = session_clock.media_us;
    uint32_t active_ms = scene.meter.active_ms;
    uint64_t percent =
        active_ms == 0 ? 0 : session_clock.busy_us / ((uint64_t)active_ms * US_PER_PERCENT_MS);
    link->busy_percent = (uint8_t)(percent < FULL_PERCENT ? percent : FULL_PERCENT);
    link->errors = stats->failed_commands;
    link->sense_key = stats->last_sense_key;
    link->sense_code = stats->last_sense_code;
    link->packet = usb_device_bulk_packet_size(&port.device);
    link->seconds = (now_ms - session_clock.started_ms) / MS_PER_SECOND;
}

static bool follow_light(DriveLight *light, uint8_t lun, uint32_t now_ms)
{
    UsbSceneUsage *usage = &scene.usage[lun];
    uint8_t shown[2] = {usage->read_glow, usage->write_glow};
    uint8_t glow[2];
    bool changed = false;
    for (int written = 0; written < 2; written++) {
        uint32_t commands = msc_lun_commands(&msc, lun, written != 0);
        if (commands != light->commands[written]) {
            light->commands[written] = commands;
            light->seen_ms[written] = now_ms;
            light->seen[written] = true;
            session_clock.traffic_ms = now_ms;
            session_clock.traffic_seen = true;
        }
        glow[written] = usb_meter_glow(light->seen[written], now_ms - light->seen_ms[written]);
        changed = changed || glow[written] / GLOW_STEP != shown[written] / GLOW_STEP ||
                  (glow[written] == 0) != (shown[written] == 0);
    }
    if (changed) {
        usage->read_glow = glow[0];
        usage->write_glow = glow[1];
    }
    return changed;
}

static bool lit_now(int written, uint32_t now_ms)
{
    bool lit = false;
    for (int lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        const DriveLight *light = &session_clock.lights[lun];
        lit = lit || usb_meter_lit(light->seen[written], now_ms - light->seen_ms[written]);
    }
    return lit;
}

static bool follow_activity(uint32_t now_ms)
{
    bool changed = false;
    for (int written = 0; written < USB_METER_DIRECTIONS; written++) {
        bool lit = lit_now(written, now_ms);
        changed = changed || lit != scene.lit[written];
        scene.lit[written] = lit;
    }
    return changed;
}

static void show_lit(Rect area, uint32_t now_ms)
{
    AppUsbStatus status = storage_status();
    app_show_usb_area(status, area);
    session_clock.lit_ms = now_ms;
}

static void follow_lights(uint32_t now_ms)
{
    if (!traffic(scene.state) || now_ms - session_clock.lit_ms < LIGHT_FRAME_MS) {
        return;
    }
    bool overview = scene.page == USB_SCENE_OVERVIEW;
    const Theme *theme = app_theme();
    for (uint8_t lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        if (follow_light(&session_clock.lights[lun], lun, now_ms) && overview) {
            show_lit(usb_screen_drive_lights(theme, storage_status(), lun), now_ms);
        }
    }
    if (follow_activity(now_ms) && overview) {
        show_lit(usb_screen_activity(theme, storage_status()), now_ms);
    }
}

static bool follow_steady(uint32_t now_ms)
{
    bool recent =
        session_clock.traffic_seen && now_ms - session_clock.traffic_ms < USB_METER_QUIET_MS;
    bool lit = scene.lit[USB_METER_READ] || scene.lit[USB_METER_WRITE];
    bool moved = usb_meter_total(&scene.meter, 0) != 0;
    scene.rate = lit || moved ? usb_meter_smooth(scene.rate, usb_meter_total(&scene.meter, 0)) : 0;
    bool changed = usb_steady_follow(&session_clock.busy, recent, now_ms);
    scene.busy = session_clock.busy.shown != 0;
    return changed;
}

static void show_sample(bool changed, bool ticked)
{
    AppUsbStatus status = storage_status();
    const Theme *theme = app_theme();
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    if (scene.page != USB_SCENE_OVERVIEW && scene.page < usb_screen_pages(theme, status)) {
        if (changed || ticked) {
            app_show_usb_area(status, usb_screen_content(theme, &layout));
        }
        return;
    }
    if (changed) {
        app_show_usb_area(status, layout.art);
        app_show_usb_area(status, usb_screen_column(&layout));
    }
}

static void sample_traffic(uint32_t now_ms)
{
    if (!usb_meter_due(&scene.meter, now_ms) || !traffic(scene.state)) {
        return;
    }
    MscStats stats = msc_stats(&msc);
    bool changed = drives_changed(&stats);
    uint32_t totals[USB_METER_DIRECTIONS] = {msc_sectors_moved(&msc, false),
                                             msc_sectors_moved(&msc, true)};
    usb_meter_sample(&scene.meter, totals, now_ms);
    uint32_t seconds = scene.link.seconds;
    follow_link(&stats, now_ms);
    bool moving_now = usb_meter_moving(&scene.meter);
    changed = follow_steady(now_ms) || changed || moving_now || session_clock.moving;
    session_clock.moving = moving_now;
    show_sample(changed, seconds != scene.link.seconds);
}

typedef struct SessionWatch {
    MscStats shown;
    HalBattery battery;
    uint32_t refreshed;
    uint32_t followed;
    UsbLeaveState state;
} SessionWatch;

static bool follow_card(void)
{
    if (msc.phase != MSC_PHASE_COMMAND) {
        return false;
    }
    if (eject_card) {
        sd_eject(true);
        eject_card = false;
    }
    BlockDevice device;
    bool ready = sd_start(&device);
    bool changed = ready != card_present;
    if (changed) {
        msc_set_media(&msc, CARD_LUN, ready, ready ? sd_sector_count() : 0);
    }
    card_present = ready;
    return changed;
}

static void watch_session(SessionWatch *watch, uint32_t now)
{
    watch->followed = now;
    bool card = follow_card();
    watch->state = follow_leave(now);
    if (scene_follow(watch->state, now) || card) {
        show_storage_state();
    }
    if (now - watch->refreshed >= COUNTER_REFRESH_MS) {
        refresh(&watch->shown, &watch->battery);
        watch->refreshed = hal_clock_milliseconds();
    }
    follow_nudge(now);
    follow_lights(now);
    sample_traffic(now);
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
        watch_speed(now);
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
    shared->last_session = (HalUsbLast){
        .end = hal_end(end),
        .left_mounted = msc_left_mounted(&msc),
        .internal_write_ok = msc_write_proven(&stats, INTERNAL_LUN),
        .internal_written = stats.lun_sectors_written[INTERNAL_LUN],
        .card_written = stats.lun_sectors_written[CARD_LUN],
        .writes_after_eject = stats.writes_after_eject,
        .speed_fallback = (uint8_t)fallback.cause,
        .fallback_commands = fallback.commands < UINT8_MAX ? (uint8_t)fallback.commands : UINT8_MAX,
        .fallback_setups = fallback_setups};
}

static UsbSessionEnd run(void)
{
    app_show_usb_status(
        (AppUsbStatus){.view = APP_USB_CONNECTING, .detail = i18n_text(STR_PLUG_COMPUTER_LINE)});
    fallback = (UsbFallback){.watching = false};
    fallback_setups = (HalUsbSetupTrace){0};
    forced = (UsbForced){0};
    leave = (UsbLeave){0};
    scene = (UsbScene){
        .state = USB_SCENE_CONNECTED, .page = shared->usb_page, .service = serve_while_drawing};
    session_clock = (SessionClock){0};
    refused_before = 0;
    debug_report_write(shared, DEBUG_REPORT_USB_SESSION);
    prepare_port();
    prepare_luns();
    usb_silence_audio();
    UsbSessionEnd end = USB_SESSION_NO_HOST;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        usb_fallback_start(&fallback, port.link.high_speed_allowed);
        usb_activity_start(&activity, hal_clock_milliseconds());
        session_clock.started_ms = hal_clock_milliseconds();
        usb_meter_start(&scene.meter, session_clock.started_ms);
        usb_steady_start(&session_clock.busy, 0, session_clock.started_ms);
        session_clock.lit_ms = session_clock.started_ms;
        scene_follow(USB_LEAVE_SERVING, hal_clock_milliseconds());
        show_storage_state();
        end = serve();
    }
    usb_session_stop_listening();
    if (end == USB_SESSION_EJECTED && traffic(scene.state)) {
        usb_screen_capture(storage_status(), &scene, CAPTURE_STORAGE, CAPTURE_STORAGE_SLOT);
    }
    remember_session(end);
    shared->show_modes = end == USB_SESSION_EJECTED || end == USB_SESSION_STOPPED;
    return end;
}

CORE_SEAM bool usb_session_storage_changed(void)
{
    MscStats stats = msc_stats(&msc);
    return stats.lun_sectors_written[INTERNAL_LUN] > 0 || stats.lun_sectors_written[CARD_LUN] > 0;
}

static void warn(uint8_t state, StringId advice)
{
    scene.state = state;
    scene.service = NULL;
    app_show_usb_status((AppUsbStatus){.view = APP_USB_STORAGE,
                                       .information = i18n_text(STR_USB_UNPLUGGED),
                                       .detail = i18n_text(advice),
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
    if (finished) {
        update_after_usb(shared);
    }
    bool script_taken = end == USB_SESSION_EJECTED && debug_script_take();
    return usb_session_storage_changed() || script_taken;
}
