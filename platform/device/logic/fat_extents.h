#pragma once

#include <stdint.h>

#include "device/fat.h"

typedef enum FatLookup {
    FAT_LOOKUP_UNMAPPED,
    FAT_LOOKUP_FOUND,
    FAT_LOOKUP_FAILED
} FatLookup;

FatLookup fat_extents_lookup(FatVolume *volume, FatFile *file, uint32_t index, uint32_t *cluster);
