#include <stddef.h>

#include "app/app.h"
#include "bluetooth_store.h"
#include "board.h"
#include "card_files.h"
#include "core/i18n.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "device/slot_copy.h"
#include "favorites_store.h"
#include "headphone_store.h"
#include "internal_volume.h"
#include "overlay.h"
#include "player.h"
#include "screen_modules.h"
#include "settings_store.h"
#include "spoken_store.h"
#include "trial_module.h"
#include "trial_record.h"
#include "usb_session.h"

#define STOCK_LABEL "ECHO"

enum {
    MESSAGE_CAPACITY = 96,
    PERCENT = 100,
    PERCENT_STEP = 5,
    SECTOR_BYTES = 512,
    OVERLAY_HEADER_WORDS = OVERLAY_HEADER_BYTES / 4
};

static bool record_read(FatVolume *volume, UpdateTrial *trial)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    FatFile record;
    if (!fat_open(volume, UPDATE_TRIAL_PATH, &record) ||
        !fat_read(volume, &record, 0, bytes, sizeof bytes)) {
        return false;
    }
    *trial = update_trial_decode(bytes);
    return true;
}

static bool record_write(FatVolume *volume, UpdateTrial trial)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    update_trial_encode(trial, bytes);
    return trial_record_store(volume, bytes);
}

static bool record_clear(FatVolume *volume)
{
    if (!record_write(volume, (UpdateTrial){UPDATE_TRIAL_NONE, 0})) {
        return false;
    }
    fat_delete(volume, UPDATE_TRIAL_PATH);
    return true;
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
    SlotValidity validity = slot_copy_check(storage, storage->slot_2, buffer);
    if (validity == SLOT_UNREADABLE) {
        return;
    }
    if (validity == SLOT_INVALID) {
        record_clear(disk->volume);
        return;
    }
    keep_diagnostics(disk->volume, buffer);
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
    UpdateTrial trial;
    if (!record_read(disk.volume, &trial)) {
        return;
    }
    if (trial.state == UPDATE_TRIAL_NONE) {
        fat_delete(disk.volume, UPDATE_TRIAL_PATH);
        return;
    }
    UpdateTrialStep step = update_trial_boot(&trial);
    if (step == UPDATE_TRIAL_STEP_ROLL_BACK) {
        roll_back(&disk, buffer);
        return;
    }
    bool rolled_back = step == UPDATE_TRIAL_STEP_REPORT;
    if (!rolled_back && !record_write(disk.volume, trial)) {
        return;
    }
    shared->trial.running = trial.state == UPDATE_TRIAL_RUNNING;
    shared->trial.promoting = trial.state == UPDATE_TRIAL_PROMOTING;
    shared->trial.rolled_back = rolled_back;
    shared->trial.settle = shared->trial.running ? USB_SETTLE_CHANGED : 0;
    shared->trial.settle |= rolled_back ? USB_SETTLE_CHANGED | USB_SETTLE_ROLLED_BACK : 0;
}

static bool settle(UsbShared *shared)
{
    if (!user_state_module_settle(shared)) {
        return false;
    }
    if ((shared->trial.settle & USB_SETTLE_ROLLED_BACK) == 0) {
        return true;
    }
    FatVolume *volume = internal_volume_mount();
    UpdateTrial trial;
    if (volume == NULL || !record_read(volume, &trial) ||
        (trial.state == UPDATE_TRIAL_ROLLED_BACK && !record_clear(volume))) {
        return false;
    }
    shared->trial.settle = 0;
    return true;
}

static bool running_from_slot_1(const UpdateStorage *storage, uint32_t *buffer,
                                const uint32_t *window)
{
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

static UpdateTrialPromotion promotion(const TrialDisk *disk, const UsbShared *shared,
                                      uint32_t *buffer, const uint32_t *running)
{
    UpdateTrial trial;
    if (!record_read(disk->volume, &trial)) {
        return UPDATE_TRIAL_KEEP;
    }
    SlotValidity validity = slot_copy_check(&disk->storage, disk->storage.slot_1, buffer);
    if (validity == SLOT_UNREADABLE) {
        return UPDATE_TRIAL_KEEP;
    }
    UpdateTrialCheck check = {.confirmed = shared->last_session.end == HAL_USB_END_EJECTED &&
                                           shared->last_session.internal_write_ok,
                              .slot_1_valid = validity == SLOT_VALID,
                              .running_slot_1 =
                                  validity == SLOT_VALID &&
                                  running_from_slot_1(&disk->storage, buffer, running)};
    return update_trial_promotion(trial.state, check);
}

static bool end_trial(FatVolume *volume, UsbShared *shared)
{
    if (!record_clear(volume)) {
        return false;
    }
    shared->trial.running = false;
    shared->trial.promoting = false;
    return true;
}

static bool promote(const TrialDisk *disk, UsbShared *shared, uint32_t *buffer)
{
    uint32_t shown = 0;
    show_backup(shown);
    if (!record_write(disk->volume, (UpdateTrial){UPDATE_TRIAL_PROMOTING, 0})) {
        return false;
    }
    shared->trial.promoting = true;
    return trial_record_copy(disk, disk->storage.slot_1, disk->storage.slot_2, buffer,
                             (UpdateProgress){report_backup, &shown}) &&
           end_trial(disk->volume, shared);
}

void trial_boot_module_session_end(UsbShared *shared, uint32_t *buffer, const uint32_t *running)
{
    TrialDisk disk;
    if (!trial_record_open(&disk)) {
        return;
    }
    UpdateTrialPromotion step = promotion(&disk, shared, buffer, running);
    if (step == UPDATE_TRIAL_CLEAR) {
        end_trial(disk.volume, shared);
        return;
    }
    if (step == UPDATE_TRIAL_PROMOTE && !promote(&disk, shared, buffer)) {
        app_show_notice(i18n_text(STR_UPDATE_FAILED), i18n_text(STR_UPDATE_FAILED_HINT));
        board_wait_for_key();
    }
}

static void rename_stock_label(void)
{
    FatVolume *volume = internal_volume_mount();
    if (volume != NULL && !usb_session_shared()->reconnect.in_session) {
        fat_fix_label(volume, FIRMWARE_NAME, STOCK_LABEL);
    }
}

static void start_with_headphones(HeadphoneStore *store)
{
    rename_stock_label();
    headphone_store_load(store);
}

const TrialBootModuleEntry TRIAL_BOOT_MODULE_ENTRY = {SCREEN_MODULE_MAGIC(TRIAL_BOOT_MODULE_NUMBER),
                                                      trial_boot_module_start,
                                                      trial_boot_module_session_end,
                                                      settle,
                                                      start_with_headphones,
                                                      settings_store_read,
                                                      player_restore,
                                                      favorites_store_read,
                                                      spoken_store_read,
                                                      bluetooth_store_load,
                                                      card_files_prepare_step};
