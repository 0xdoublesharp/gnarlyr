#include "trial_record.h"

#include <stddef.h>

#include "device/slot_copy.h"
#include "emmc.h"
#include "internal_volume.h"

enum {
    SLOT_CHUNKS = 0x500000 / UPDATER_CHUNK_BYTES
};

bool trial_record_open(TrialDisk *disk)
{
    EmmcInfo info;
    if (!emmc_start(&info)) {
        return false;
    }
    disk->volume = internal_volume_mount();
    disk->storage =
        (UpdateStorage){emmc_read,          emmc_write,         NULL,
                        info.layout.slot_1, info.layout.slot_2, info.layout.slot_sectors};
    return disk->volume != NULL;
}

bool trial_record_store(FatVolume *volume, const uint8_t *bytes)
{
    FatFile record;
    FatReserveResult reserved = fat_reserve(volume, UPDATE_TRIAL_PATH, UPDATE_TRIAL_FILE_BYTES);
    return (reserved == FAT_RESERVE_EXISTS || reserved == FAT_RESERVE_CREATED) &&
           fat_open(volume, UPDATE_TRIAL_PATH, &record) &&
           fat_write(volume, &record, 0, bytes, UPDATE_TRIAL_BYTES);
}

bool trial_record_copy(const TrialDisk *disk, uint32_t from, uint32_t to, uint32_t *buffer,
                       UpdateProgress progress)
{
    SlotCopyJob job = {&disk->storage, NULL, from, to, buffer, progress, SLOT_CHUNKS};
    return slot_copy_run(&job);
}
