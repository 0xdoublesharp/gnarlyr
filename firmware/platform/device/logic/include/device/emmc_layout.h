#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/block_device.h"

typedef struct EmmcLayout {
    uint32_t slot_1;
    uint32_t slot_2;
    uint32_t slot_sectors;
    uint32_t firmware_end;
    uint32_t volume_start;
    uint32_t volume_sectors;
} EmmcLayout;

typedef enum EmmcSlot2 {
    EMMC_SLOT_2_HALF,
    EMMC_SLOT_2_SEARCH
} EmmcSlot2;

enum {
    EMMC_LAYOUT_SCRATCH_WORDS = 3 * 512 / 4
};

typedef struct EmmcLayoutRead {
    EmmcSlot2 placement;
    uint32_t *scratch;
} EmmcLayoutRead;

bool emmc_layout_read(EmmcLayout *layout, const BlockDevice *device, uint32_t total_sectors);
bool emmc_layout_read_with(EmmcLayout *layout, const BlockDevice *device, uint32_t total_sectors,
                           EmmcLayoutRead read);
bool emmc_layout_in_volume(const EmmcLayout *layout, uint32_t lba, uint32_t count);
bool emmc_layout_allows_write(const EmmcLayout *layout, uint32_t lba, uint32_t count);
