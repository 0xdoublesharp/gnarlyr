#include "device/storage_mount.h"

#include <stddef.h>

static bool mount_from(FatVolume *volume, StorageStart start)
{
    BlockDevice device;
    return start(&device) && fat_mount(volume, device);
}

StorageKind storage_mount(FatVolume *volume, StorageStart card, StorageStart internal)
{
    if (mount_from(volume, card)) {
        fat_fix_label(volume, "GnarlySD", NULL);
        return STORAGE_CARD;
    }
    if (mount_from(volume, internal)) {
        fat_fix_label(volume, FIRMWARE_NAME, "ECHO");
        return STORAGE_INTERNAL;
    }
    return STORAGE_NONE;
}
