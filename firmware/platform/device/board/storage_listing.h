#pragma once

#include "device/fat.h"
#include "hal/storage.h"

FatDirEntry *storage_listing_begin(const HalDir *dir, FatVolume **mounted_volume,
                                   FatDirCursor **cursor);
