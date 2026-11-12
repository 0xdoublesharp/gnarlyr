#include "device/storage_mount.h"

#include <stddef.h>

#include "device/card_format.h"

static bool mount_from(FatVolume *volume, StorageStart start)
{
    BlockDevice device;
    return start(&device) && fat_mount(volume, device);
}

StorageKind storage_mount(FatVolume *volume, StorageStart card, StorageStart internal)
{
    if (mount_from(volume, card)) {
        fat_fix_label(volume, CARD_FORMAT_LABEL, NULL);
        return STORAGE_CARD;
    }
    return mount_from(volume, internal) ? STORAGE_INTERNAL : STORAGE_NONE;
}
