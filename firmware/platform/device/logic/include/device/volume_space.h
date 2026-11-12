#pragma once

#include <stdint.h>

#include "device/block_device.h"

enum {
    VOLUME_SPACE_SECTOR = 512,
    VOLUME_SPACE_BITMAP_SECTORS = 8192
};

#define VOLUME_SPACE_UNKNOWN 0xFFFFFFFFu

typedef struct VolumeSpaceBuffer {
    uint32_t *words;
    uint32_t sectors;
} VolumeSpaceBuffer;

uint32_t volume_space_free_sectors(BlockDevice device, VolumeSpaceBuffer buffer);
