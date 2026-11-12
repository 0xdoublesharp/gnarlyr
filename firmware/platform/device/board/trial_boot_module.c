#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "device/slot_copy.h"
#include "overlay.h"
#include "screen_modules.h"
#include "trial_module.h"
#include "trial_record.h"
#include "usb_session.h"

enum {
    MESSAGE_CAPACITY = 96,
    PERCENT = 100,
    PERCENT_STEP = 5,
    SECTOR_BYTES = 512,
    OVERLAY_HEADER_WORDS = OVERLAY_HEADER_BYTES / 4
};

static UpdateTrial record_read(FatVolume *volume)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    FatFile record;
    if (!fat_open(volume, UPDATE_TRIAL_PATH, &record) ||
        !fat_read(volume, &record, 0, bytes, sizeof bytes)) {
        return (UpdateTrial){UPDATE_TRIAL_NONE, 0};
    }
    return update_trial_decode(bytes);
}

static bool record_write(FatVolume *volume, UpdateTrial trial)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    if (trial.state == UPDATE_TRIAL_NONE) {
        fat_delete(volume, UPDATE_TRIAL_PATH);
        return true;
    }
    update_trial_encode(trial, bytes);
    return trial_record_store(volume, bytes);
}

static void keep_diagnostics(FatVolume *volume, uint32_t *buffer)
{
    FatFile file;
    if (!fat_open(volume, UPDATE_TRIAL_PATH, &file) ||
        !fat_read(volume, &file, 0, buffer, UPDATE_TRIAL_FILE_BYTES)) {
        return;
    }
    FatReserveResult reserved =
        fat_reserve(volume, UPDATE_TRIAL_DIAG_PATH, UPDATE_TRIAL_FILE_BYTES);
    if ((reserved == FAT_RESERVE_EXISTS || reserved == FAT_RESERVE_CREATED) &&
        fat_open(volume, UPDATE_TRIAL_DIAG_PATH, &file)) {
        fat_write(volume, &file, 0, buffer, UPDATE_TRIAL_FILE_BYTES);
    }
}

static void roll_back(const TrialDisk *disk, uint32_t *buffer)
{
    const UpdateStorage *storage = &disk->storage;
    keep_diagnostics(disk->volume, buffer);
    if (!slot_copy_is_valid(storage, storage->slot_2, buffer)) {
        record_write(disk->volume, (UpdateTrial){UPDATE_TRIAL_NONE, 0});
        return;
    }
    if (trial_record_copy(disk, storage->slot_2, storage->slot_1, buffer,
                          (UpdateProgress){NULL, NULL}) &&
        record_write(disk->volume, (UpdateTrial){UPDATE_TRIAL_ROLLED_BACK, 0})) {
        board_restart_clean();
    }
}

CORE_SEAM void trial_boot_module_start(UsbShared *shared, uint32_t *buffer)
{
    TrialDisk disk;
    if (!trial_record_open(&disk)) {
        return;
    }
    UpdateTrial trial = record_read(disk.volume);
    UpdateTrialStep step = update_trial_boot(&trial);
    if (step == UPDATE_TRIAL_STEP_ROLL_BACK) {
        roll_back(&disk, buffer);
        return;
    }
    if (!record_write(disk.volume, trial)) {
        return;
    }
    shared->trial.running = trial.state == UPDATE_TRIAL_RUNNING;
    shared->trial.promoting = trial.state == UPDATE_TRIAL_PROMOTING;
    shared->trial.rolled_back = step == UPDATE_TRIAL_STEP_REPORT;
}

static bool running_from_slot_1(const UpdateStorage *storage, uint32_t *buffer)
{
    const uint32_t *window = (const uint32_t *)(uintptr_t)OVERLAY_WINDOW_BASE;
    if (!storage->read(NULL, storage->slot_1 + USB_OVERLAY_IMAGE_OFFSET / SECTOR_BYTES, 1,
                       buffer)) {
        return false;
    }
    for (uint32_t index = 0; index < OVERLAY_HEADER_WORDS; index++) {
        if (buffer[index] != window[index]) {
            return false;
        }
    }
    return true;
}

static void show_backup(uint32_t percent)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_UPDATE_PROGRESS, percent, 0);
    app_show_notice(i18n_text(STR_UPDATE_BACKUP), text);
    board_display_service();
}

static void report_backup(void *context, uint32_t done, uint32_t total)
{
    uint32_t *shown = context;
    uint32_t percent = done * PERCENT / total;
    if (percent >= *shown + PERCENT_STEP) {
        *shown = percent;
        show_backup(percent);
    }
}

static bool promotion_due(const TrialDisk *disk, const UsbShared *shared, uint32_t *buffer)
{
    UpdateTrial trial = record_read(disk->volume);
    bool confirmed =
        trial.state == UPDATE_TRIAL_RUNNING && shared->last_session.end == HAL_USB_END_EJECTED;
    return (confirmed || trial.state == UPDATE_TRIAL_PROMOTING) &&
           running_from_slot_1(&disk->storage, buffer);
}

void trial_boot_module_session_end(UsbShared *shared, uint32_t *buffer)
{
    TrialDisk disk;
    if (!trial_record_open(&disk) || !promotion_due(&disk, shared, buffer)) {
        return;
    }
    uint32_t shown = 0;
    show_backup(0);
    if (record_write(disk.volume, (UpdateTrial){UPDATE_TRIAL_PROMOTING, 0}) &&
        trial_record_copy(&disk, disk.storage.slot_1, disk.storage.slot_2, buffer,
                          (UpdateProgress){report_backup, &shown}) &&
        record_write(disk.volume, (UpdateTrial){UPDATE_TRIAL_NONE, 0})) {
        shared->trial.running = false;
        shared->trial.promoting = false;
        return;
    }
    app_show_notice(i18n_text(STR_UPDATE_FAILED), i18n_text(STR_UPDATE_FAILED_HINT));
    board_wait_for_key();
}

const TrialBootModuleEntry TRIAL_BOOT_MODULE_ENTRY = {SCREEN_MODULE_MAGIC(TRIAL_BOOT_MODULE_NUMBER),
                                                      trial_boot_module_start,
                                                      trial_boot_module_session_end};
