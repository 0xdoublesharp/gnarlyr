#include "trial.h"

#include <stddef.h>
#include <string.h>

#include "app/app.h"
#include "audio_process.h"
#include "board.h"
#include "core/deadline.h"
#include "core/i18n.h"
#include "core/seam.h"
#include "device/fat.h"
#include "device/key_ladder.h"
#include "device/slot_copy.h"
#include "device/update_trial.h"
#include "emmc.h"
#include "favorites_store.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "hal/system.h"
#include "headphone_store.h"
#include "internal_volume.h"
#include "mmc.h"
#include "overlay.h"
#include "player.h"
#include "settings_store.h"
#include "spoken_store.h"
#include "trial_module.h"
#include "usb.h"
#include "usb_session.h"

enum {
    SECTOR_WORDS = 512 / 4,
    ESCAPE_LADDER_KEY = 1u << KEY_VOLUME_UP,
    ESCAPE_HOLD_MS = 3000,
    DIAG_SNAPSHOT_WORDS = 18,
    DIAG_REPORT_WORDS = 4,
    DIAG_SPARE_WORDS = DIAG_SNAPSHOT_WORDS - DIAG_REPORT_WORDS - sizeof(MmcFailure) / 4 - 5,
    DIAG_AT = 2,
    KEEP_ATTEMPTS = 0xFF
};

#define RECORD_MAGIC 0x54555344u
#define RECORD_RUNNING 0x015B0001u
#define DIAG_MAGIC 0x47414944u

typedef struct TrialSnapshot {
    uint32_t uptime;
    uint32_t clock;
    uint32_t report[DIAG_REPORT_WORDS];
    uint32_t link;
    MmcFailure failure;
    uint32_t session;
    uint32_t written;
    uint32_t spare[DIAG_SPARE_WORDS];
} TrialSnapshot;

typedef struct TrialDiag {
    uint32_t magic;
    TrialSnapshot snapshots[2];
} TrialDiag;

_Static_assert(sizeof(UsbReport) >= DIAG_REPORT_WORDS * 4 &&
                   sizeof(TrialSnapshot) == DIAG_SNAPSHOT_WORDS * 4,
               "trial diagnostics preserve the sector format");

static uint32_t power_since_ms;
static uint32_t escape_since_ms;
static bool power_released;
static bool boot_module_failed;
static uint32_t boot_module_failed_at;
__attribute__((section(".noinit"))) static TrialDiag diag;

void board_slot_cache_invalidate(void);

static uint32_t *copy_buffer(void)
{
    return (uint32_t *)(uintptr_t)(OVERLAY_WINDOW_BASE + OVERLAY_WINDOW_BYTES -
                                   SLOT_COPY_BUFFER_WORDS * 4);
}

enum {
    BOOT_MODULE_RETRY_MS = 10000
};

static const TrialBootModuleEntry *boot_module(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    if (boot_module_failed &&
        !deadline_elapsed(now_ms, boot_module_failed_at, BOOT_MODULE_RETRY_MS)) {
        return NULL;
    }
    const TrialBootModuleEntry *entry = hal_system_load_module(TRIAL_BOOT_MODULE_NUMBER);
    boot_module_failed = entry == NULL;
    if (boot_module_failed) {
        boot_module_failed_at = now_ms;
    }
    return entry;
}

static uint32_t record_locate(FatVolume *volume)
{
    FatFile record;
    uint32_t lba;
    if (volume != NULL && fat_open(volume, UPDATE_TRIAL_PATH, &record) &&
        record.size >= UPDATE_TRIAL_FILE_BYTES && fat_file_sector(volume, &record, 0, &lba)) {
        return lba;
    }
    return 0;
}

static uint32_t record_header(uint32_t attempts)
{
    return RECORD_RUNNING ^ (attempts << 16 | attempts << 8);
}

static bool record_running(const uint32_t *words)
{
    return words[0] == RECORD_MAGIC && words[1] == record_header(words[1] >> 8 & 0xFFu);
}

__attribute__((noinline)) static bool record_commit(uint32_t lba, uint32_t *sector,
                                                    uint32_t attempts)
{
    if (attempts <= UPDATE_TRIAL_LIMIT + 1) {
        sector[1] = record_header(attempts);
    }
    if (diag.magic == DIAG_MAGIC) {
        memcpy(sector + DIAG_AT, &diag, sizeof diag);
        diag.magic = 0;
    }
    return emmc_volume_write(NULL, lba, 1, sector);
}

static bool count_start(UsbTrial *trial, uint32_t *sector)
{
    uint32_t attempts = sector[1] >> 8 & 0xFFu;
    if (!record_running(sector) || attempts >= UPDATE_TRIAL_LIMIT) {
        return false;
    }
    trial->attempts = (uint8_t)(attempts + 1);
    trial->running = true;
    trial->settle = USB_SETTLE_CHANGED;
    trial->charged = record_commit(trial->record_lba, sector, trial->attempts);
    trial->session_due = trial->charged;
    return true;
}

void board_trial_boot(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    uint32_t sector[SECTOR_WORDS];
    trial->record_lba = record_locate(internal_volume_mount());
    if (trial->record_lba == 0 || !emmc_volume_read(NULL, trial->record_lba, 1, sector) ||
        count_start(trial, sector)) {
        return;
    }
    const TrialBootModuleEntry *entry = boot_module();
    if (entry != NULL) {
        entry->start(usb_session_shared(), copy_buffer());
    }
}

CORE_SEAM void headphone_store_load(HeadphoneStore *store)
{
    const TrialBootModuleEntry *entry = boot_module();
    if (entry != NULL) {
        entry->headphones_load(store);
        return;
    }
    *store = (HeadphoneStore){.profiles = {.current = HEADPHONE_PROFILE_NONE},
                              .active = {.left = REPLAY_GAIN_UNITY, .right = REPLAY_GAIN_UNITY},
                              .load = STORE_UNSUPPORTED};
    store->file.needs_reload = true;
    hal_audio_set_headphones(store->active);
}

CORE_SEAM void settings_store_read(SettingsStore *store)
{
    const TrialBootModuleEntry *entry = boot_module();
    if (entry != NULL) {
        entry->settings_read(store);
        return;
    }
    store->settings = settings_defaults();
    store->has_saved = false;
    store->has_saved_buttons = false;
    store->unread = true;
}

CORE_SEAM bool player_restore(Player *player, bool cold_start)
{
    const TrialBootModuleEntry *entry = boot_module();
    player->storage.write_blocked = entry == NULL;
    return entry != NULL && entry->player_restore(player, cold_start);
}

CORE_SEAM StoreLoad favorites_store_read(Favorites *favorites)
{
    const TrialBootModuleEntry *entry = boot_module();
    return entry != NULL ? entry->favorites_read(favorites) : STORE_UNSUPPORTED;
}

CORE_SEAM StoreLoad spoken_store_read(Bookmarks *bookmarks)
{
    const TrialBootModuleEntry *entry = boot_module();
    return entry != NULL ? entry->spoken_read(bookmarks) : STORE_UNSUPPORTED;
}

bool hal_system_settle_user_state(bool requested)
{
    UsbShared *shared = usb_session_shared();
    if (!requested && shared->trial.settle == 0) {
        return true;
    }
    const TrialBootModuleEntry *entry = boot_module();
    if (entry == NULL) {
        return false;
    }
    board_audio_shut_down();
    board_audio_process_forget();
    return entry->settle(shared);
}

void board_trial_report(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->rolled_back) {
        trial->rolled_back = false;
        app_show_notice(i18n_text(STR_UPDATE_ROLLED_BACK), i18n_text(STR_UPDATE_ROLLED_BACK_HINT));
        board_wait_for_key();
    }
}

__attribute__((noinline, noclone)) static void write_record(uint32_t lba, uint32_t attempts)
{
    uint32_t sector[SECTOR_WORDS];
    if (lba != 0 && emmc_volume_read(NULL, lba, 1, sector) && record_running(sector)) {
        record_commit(lba, sector, attempts);
    }
}

static void diag_snapshot(TrialSnapshot *snapshot, const UsbShared *shared)
{
    snapshot->failure = mmc_last_failure();
    snapshot->uptime = hal_clock_milliseconds();
    snapshot->clock = hal_system_clock_mhz() | (uint32_t)shared->high_speed_failed << 16;
    memcpy(snapshot->report, &shared->report, sizeof snapshot->report);
    snapshot->link = (uint32_t)shared->link.step | (uint32_t)shared->link.high_speed_allowed << 8 |
                     (uint32_t)shared->link.dedicated_fifos << 16;
    snapshot->session =
        (uint32_t)shared->last_session.end | (uint32_t)shared->last_session.internal_write_ok << 8;
    snapshot->written = shared->last_session.internal_written;
}

void board_trial_snapshot(uint32_t kind)
{
    UsbShared *shared = usb_session_shared();
    if (hal_system_restart_pending()) {
        shared->trial.record_lba = 0;
        board_slot_cache_invalidate();
        return;
    }
    if (!shared->trial.running) {
        return;
    }
    diag_snapshot(&diag.snapshots[kind], shared);
    if (kind != 0) {
        shared->trial.record_lba = record_locate(internal_volume_remount());
    }
    diag.magic = DIAG_MAGIC;
    write_record(shared->trial.record_lba, KEEP_ATTEMPTS);
}

void board_trial_settle(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->charged && i18n_attached() && hal_clock_milliseconds() >= UPDATE_TRIAL_SETTLE_MS) {
        trial->charged = false;
        write_record(trial->record_lba, trial->attempts - 1u);
    }
}

void board_trial_fail(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->charged) {
        write_record(trial->record_lba, UPDATE_TRIAL_LIMIT);
        board_restart_clean();
    }
}

bool board_trial_running(void)
{
    return usb_session_shared()->trial.running;
}

void board_trial_session_starting(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->session_due) {
        trial->session_due = false;
        write_record(trial->record_lba, trial->charged ? trial->attempts + 1u : trial->attempts);
    }
    trial->charged = false;
}

bool board_trial_session_ended(void)
{
    UsbShared *shared = usb_session_shared();
    bool confirmed = shared->trial.running && shared->last_session.end == HAL_USB_END_EJECTED &&
                     shared->last_session.internal_write_ok;
    const TrialBootModuleEntry *entry = confirmed || shared->trial.promoting ? boot_module() : NULL;
    if (entry == NULL) {
        return false;
    }
    entry->session_end(shared, copy_buffer());
    board_slot_cache_invalidate();
    return !shared->trial.running && !shared->trial.promoting;
}

static bool escape_held(void)
{
    bool power = board_power_key_down();
    power_released |= !power;
    return power && (power_released || !board_start_after_loader()) && usb_vbus_present() &&
           (board_input_held() & ~(1u << KEY_POWER)) == ESCAPE_LADDER_KEY;
}

bool hal_input_loader_keys_held(void)
{
    return escape_held();
}

void board_trial_poll_power(uint32_t now_ms)
{
    if (!escape_held()) {
        escape_since_ms = now_ms;
    } else if (deadline_elapsed(now_ms, escape_since_ms, ESCAPE_HOLD_MS)) {
        board_loader_reboot();
    }
    if (!usb_session_shared()->trial.running || !board_power_key_down()) {
        power_since_ms = now_ms;
    } else if (deadline_elapsed(now_ms, power_since_ms, UPDATE_TRIAL_RESET_HOLD_MS)) {
        board_restart();
    }
}
