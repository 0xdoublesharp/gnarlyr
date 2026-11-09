#pragma once

#include <stdbool.h>

#include "device/block_device.h"
#include "device/fat.h"

typedef enum StorageKind {
    STORAGE_NONE,
    STORAGE_CARD,
    STORAGE_INTERNAL
} StorageKind;

typedef bool (*StorageStart)(BlockDevice *device);

StorageKind storage_mount(FatVolume *volume, StorageStart card, StorageStart internal);
