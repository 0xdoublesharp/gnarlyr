#include "trial_module.h"

#include <stddef.h>

#include "core/seam.h"
#include "device/slot_copy.h"
#include "trial_record.h"

enum {
    SLOT_CHUNKS = 0x500000 / UPDATER_CHUNK_BYTES
};

static const uint8_t STARTED[UPDATE_TRIAL_BYTES] = UPDATE_TRIAL_STARTED;

static bool install_trial(const TrialDisk *disk, const UpdateFile *update, uint32_t *buffer,
                          UpdateProgress progress)
{
    if (!trial_record_store(disk->volume, STARTED)) {
        return false;
    }
    SlotCopyJob job = {&disk->storage, update,   0,          disk->storage.slot_1,
                       buffer,         progress, SLOT_CHUNKS};
    if (slot_copy_run(&job)) {
        return true;
    }
    if (slot_copy_is_valid(&disk->storage, disk->storage.slot_1, buffer)) {
        fat_delete(disk->volume, UPDATE_TRIAL_PATH);
    }
    return false;
}

CORE_SEAM bool trial_module_install(const UpdateFile *update, uint32_t *buffer,
                                    UpdateProgress progress, bool both)
{
    TrialDisk disk;
    if (!trial_record_open(&disk)) {
        return false;
    }
    if (both || !slot_copy_is_valid(&disk.storage, disk.storage.slot_2, buffer)) {
        if (!updater_install(update, &disk.storage, progress, buffer)) {
            return false;
        }
        fat_delete(disk.volume, UPDATE_TRIAL_PATH);
        return true;
    }
    return install_trial(&disk, update, buffer, progress);
}

const TrialCalls TRIAL_CALLS = {trial_module_install};
