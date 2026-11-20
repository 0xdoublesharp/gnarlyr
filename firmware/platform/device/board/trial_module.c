#include "trial_module.h"

#include <stddef.h>

#include "core/seam.h"
#include "device/slot_copy.h"

enum {
    SLOT_CHUNKS = 0x500000 / UPDATER_CHUNK_BYTES
};

static const uint8_t STARTED[UPDATE_TRIAL_BYTES] = UPDATE_TRIAL_STARTED;

static bool install_trial(const TrialInstall *install)
{
    const TrialDisk *disk = &install->disk;
    if (!trial_record_store(disk->volume, STARTED)) {
        return false;
    }
    SlotCopyJob job = {&disk->storage,  install->update,   0,          disk->storage.slot_1,
                       install->buffer, install->progress, SLOT_CHUNKS};
    if (slot_copy_run(&job)) {
        return true;
    }
    if (slot_copy_is_valid(&disk->storage, disk->storage.slot_1, install->buffer)) {
        fat_delete(disk->volume, UPDATE_TRIAL_PATH);
    }
    return false;
}

CORE_SEAM bool trial_module_install(const TrialInstall *install, bool both)
{
    const TrialDisk *disk = &install->disk;
    if (both || !slot_copy_is_valid(&disk->storage, disk->storage.slot_2, install->buffer)) {
        if (!updater_install(install->update, &disk->storage, install->progress, install->buffer)) {
            return false;
        }
        fat_delete(disk->volume, UPDATE_TRIAL_PATH);
        return true;
    }
    return install_trial(install);
}
