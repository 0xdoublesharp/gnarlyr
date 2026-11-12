#include "usb_session.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "debug_probe.h"
#include "device/fat.h"
#include "device/update_names.h"
#include "device/update_trial.h"
#include "device/updater.h"
#include "emmc.h"
#include "hal/power.h"
#include "internal_volume.h"
#include "overlay.h"

enum {
    MESSAGE_CAPACITY = 96,
    PERCENT_STEP = 5,
    PERCENT = 100,
    RESTART_HOLD_MS = 2000,
    BOTH_SLOTS_TEXT_BYTES = 10,
    SECTOR_BYTES = 512,
    OVERLAY_HEADER_WORDS = OVERLAY_HEADER_BYTES / 4
};

static char update_path[UPDATE_NAME_CAPACITY];
static StringId install_title;

static FatVolume *volume;
static FatFile file;
static uint32_t last_percent;
static UsbShared *shared;

static bool read_update(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    return fat_read(volume, &file, offset, buffer, length);
}

static void show_percent(uint32_t percent)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_UPDATE_PROGRESS, percent, 0);
    app_show_notice(i18n_text(install_title), text);
    board_display_service();
}

static void report(void *context, uint32_t done, uint32_t total)
{
    (void)context;
    uint32_t percent = done * PERCENT / total;
    if (percent >= last_percent + PERCENT_STEP) {
        last_percent = percent;
        show_percent(percent);
    }
}

static void remove_update_file(void)
{
    fat_delete(volume, update_path);
}

static bool open_highest_numbered(void)
{
    for (uint32_t index = 0; index < update_name_count(); index++) {
        if (update_name_at(index, update_path) && fat_open(volume, update_path, &file)) {
            return true;
        }
    }
    return false;
}

static bool mount_volume(EmmcInfo *info)
{
    if (!emmc_start(info)) {
        return false;
    }
    volume = internal_volume_remount();
    return volume != NULL;
}

static bool open_storage(UpdateStorage *storage)
{
    EmmcInfo info;
    if (!mount_volume(&info)) {
        return false;
    }
    *storage = (UpdateStorage){emmc_read,          emmc_write,         NULL,
                               info.layout.slot_1, info.layout.slot_2, info.layout.slot_sectors};
    _Static_assert(UPDATER_SCRATCH_WORDS * 4 <= 64 * 512, "the USB write buffer holds the scratch");
    updater_use_scratch(usb_session_buffer());
    return true;
}

static UpdateTrial trial_read(void)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    FatFile record;
    if (!fat_open(volume, UPDATE_TRIAL_PATH, &record) ||
        !fat_read(volume, &record, 0, bytes, sizeof bytes)) {
        return (UpdateTrial){UPDATE_TRIAL_NONE, 0};
    }
    return update_trial_decode(bytes);
}

static bool trial_write(UpdateTrial trial)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    FatFile record;
    update_trial_encode(trial, bytes);
    if (trial.state == UPDATE_TRIAL_NONE) {
        if (fat_open(volume, UPDATE_TRIAL_PATH, &record)) {
            fat_write(volume, &record, 0, bytes, sizeof bytes);
        }
        fat_delete(volume, UPDATE_TRIAL_PATH);
        return true;
    }
    FatReserveResult reserved = fat_reserve(volume, UPDATE_TRIAL_PATH, UPDATE_TRIAL_FILE_BYTES);
    return (reserved == FAT_RESERVE_EXISTS || reserved == FAT_RESERVE_CREATED) &&
           fat_open(volume, UPDATE_TRIAL_PATH, &record) &&
           fat_write(volume, &record, 0, bytes, sizeof bytes);
}

static bool running_from_slot_1(const UpdateStorage *storage)
{
    uint32_t sector[SECTOR_BYTES / 4];
    const uint32_t *window = (const uint32_t *)(uintptr_t)OVERLAY_WINDOW_BASE;
    if (!storage->read(NULL, storage->slot_1 + USB_OVERLAY_IMAGE_OFFSET / SECTOR_BYTES, 1,
                       sector)) {
        return false;
    }
    for (uint32_t index = 0; index < OVERLAY_HEADER_WORDS; index++) {
        if (sector[index] != window[index]) {
            return false;
        }
    }
    return true;
}

static bool promotion_due(const UpdateStorage *storage, UpdateTrial trial)
{
    bool confirmed =
        trial.state == UPDATE_TRIAL_RUNNING && shared->last_session.end == HAL_USB_END_EJECTED;
    bool backup_missing = !updater_slot_is_valid(storage, storage->slot_2);
    return (confirmed || backup_missing || trial.state == UPDATE_TRIAL_PROMOTING) &&
           running_from_slot_1(storage);
}

static bool promote(const UpdateStorage *storage)
{
    if (!promotion_due(storage, trial_read())) {
        return true;
    }
    install_title = STR_UPDATE_BACKUP;
    last_percent = 0;
    show_percent(0);
    bool copied = trial_write((UpdateTrial){UPDATE_TRIAL_PROMOTING, 0}) &&
                  updater_copy_slot(storage, storage->slot_1, storage->slot_2,
                                    (UpdateProgress){report, NULL});
    if (copied && trial_write((UpdateTrial){UPDATE_TRIAL_NONE, 0})) {
        return true;
    }
    app_show_notice(i18n_text(STR_UPDATE_FAILED), i18n_text(STR_UPDATE_FAILED_HINT));
    board_wait_for_key();
    return false;
}

static bool both_slots_requested(void)
{
    uint8_t bytes[BOTH_SLOTS_TEXT_BYTES];
    FatFile request;
    return fat_open(volume, UPDATE_BOTH_SLOTS_PATH, &request) &&
           request.size >= BOTH_SLOTS_TEXT_BYTES &&
           fat_read(volume, &request, 0, bytes, sizeof bytes) &&
           update_trial_both_slots_requested(bytes, sizeof bytes);
}

static bool write_slots(const UpdateFile *update, const UpdateStorage *storage, bool both)
{
    UpdateProgress progress = {report, NULL};
    if (both) {
        bool installed = updater_install(update, storage, progress);
        fat_delete(volume, UPDATE_BOTH_SLOTS_PATH);
        return installed && trial_write((UpdateTrial){UPDATE_TRIAL_NONE, 0});
    }
    if (!trial_write((UpdateTrial){UPDATE_TRIAL_RUNNING, 0})) {
        return false;
    }
    if (updater_install_trial(update, storage, progress)) {
        return true;
    }
    if (updater_slot_is_valid(storage, storage->slot_1)) {
        trial_write((UpdateTrial){UPDATE_TRIAL_NONE, 0});
    }
    return false;
}

static void install(const UpdateFile *update, const UpdateStorage *storage, UpdateCheck check)
{
    bool stock = check == UPDATE_STOCK;
    install_title = stock ? STR_UPDATE_INSTALLING_STOCK : STR_UPDATE_INSTALLING;
    last_percent = 0;
    show_percent(0);
    bool both = stock || both_slots_requested() || !updater_slot_is_valid(storage, storage->slot_2);
    shared->flash_rewritten = true;
    bool installed = write_slots(update, storage, both);
    if (updater_should_remove_file(check, installed)) {
        remove_update_file();
    }
    if (!installed) {
        app_show_notice(i18n_text(STR_UPDATE_FAILED), i18n_text(STR_UPDATE_FAILED_HINT));
        board_wait_for_key();
        return;
    }
    debug_restart_after_install(volume, shared);
    const char *title = i18n_text(stock ? STR_UPDATE_INSTALLED_STOCK : STR_UPDATE_INSTALLED);
    app_show_notice(title, i18n_text(STR_UPDATE_UNPLUG));
    do {
        board_wait_for_power_hold(RESTART_HOLD_MS);
        app_show_notice(title, i18n_text(STR_UPDATE_UNPLUG_FIRST));
    } while (hal_power_battery().usb_power);
    app_show_notice(title, i18n_text(STR_UPDATE_TURNING_OFF));
    board_display_service();
    hal_power_off();
}

static void show_refused(const char *name)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_text(&builder, STR_UPDATE_REFUSED, name);
    app_show_notice(i18n_text(STR_UPDATE_NOT_INSTALLED), text);
    board_wait_for_key();
}

void update_after_usb(UsbShared *state)
{
    UpdateStorage storage;
    shared = state;
    if (!open_storage(&storage) || !promote(&storage) || !open_highest_numbered()) {
        return;
    }
    UpdateFile update = {read_update, file.size, NULL};
    const char *name = update_path + 1;
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_text(&builder, STR_UPDATE_CHECKING, name);
    app_show_notice(i18n_text(STR_UPDATE), text);
    UpdateCheck check = updater_check(&update, &storage);
    if (check == UPDATE_VALID || check == UPDATE_STOCK) {
        install(&update, &storage, check);
        return;
    }
    if (updater_should_remove_file(check, false)) {
        remove_update_file();
    }
    if (check == UPDATE_INVALID) {
        show_refused(name);
    }
}

void update_trial_session_started(void)
{
    EmmcInfo info;
    if (!mount_volume(&info)) {
        return;
    }
    UpdateTrial trial = trial_read();
    if (update_trial_charge(&trial)) {
        trial_write(trial);
    }
}

static void roll_back(const UpdateStorage *storage)
{
    if (!updater_slot_is_valid(storage, storage->slot_2)) {
        trial_write((UpdateTrial){UPDATE_TRIAL_NONE, 0});
        return;
    }
    if (updater_copy_slot(storage, storage->slot_2, storage->slot_1,
                          (UpdateProgress){NULL, NULL}) &&
        trial_write((UpdateTrial){UPDATE_TRIAL_ROLLED_BACK, 0})) {
        board_restart();
    }
}

static void remember_settled(UpdateTrial charged)
{
    FatFile record;
    UpdateTrial settled = charged;
    update_trial_settle(&settled);
    update_trial_encode(settled, (uint8_t *)shared->trial.settled);
    shared->trial.charged = fat_open(volume, UPDATE_TRIAL_PATH, &record) &&
                            fat_file_sector(volume, &record, 0, &shared->trial.record_lba);
}

CORE_SEAM void update_trial_start(UsbShared *state)
{
    UpdateStorage storage;
    shared = state;
    if (!open_storage(&storage)) {
        return;
    }
    UpdateTrial trial = trial_read();
    UpdateTrialStep step = update_trial_boot(&trial);
    if (step == UPDATE_TRIAL_STEP_ROLL_BACK) {
        roll_back(&storage);
        return;
    }
    if (!trial_write(trial)) {
        return;
    }
    shared->trial.running = trial.state == UPDATE_TRIAL_RUNNING;
    shared->trial.rolled_back = step == UPDATE_TRIAL_STEP_REPORT;
    if (step == UPDATE_TRIAL_STEP_CHARGED) {
        remember_settled(trial);
    }
}
