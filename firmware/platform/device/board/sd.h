#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/block_device.h"

bool sd_start(BlockDevice *device);
uint32_t sd_sector_count(void);
bool sd_read(void *context, uint32_t lba, uint32_t count, void *buffer);
bool sd_write(void *context, uint32_t lba, uint32_t count, const void *buffer);

bool sd_present(void);
uint32_t sd_generation(void);
void sd_forget(void);
void sd_eject(bool eject);
bool sd_ejected(void);
uint32_t sd_allocation_unit(void);

struct SdCard;
struct MmcHost;
extern const struct MmcHost SD_MMC_HOST;
const struct SdCard *sd_card_info(void);
bool sd_card_ready(void);
