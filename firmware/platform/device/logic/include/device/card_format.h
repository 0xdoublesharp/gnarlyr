#pragma once

#include "device/block_device.h"

#define CARD_FORMAT_LABEL "GnarlySD"
#define CARD_FORMAT_LABEL_PADDED CARD_FORMAT_LABEL "   "

enum {
    CARD_FORMAT_SECTOR_BYTES = 512,
    CARD_FORMAT_BUFFER_SECTORS = 8,
    CARD_FORMAT_SDHC_SECTORS = 32u * 1024 * 1024 * 2
};

typedef struct CardFormatPlan {
    uint32_t sectors;
    uint32_t alignment;
    uint32_t start;
    uint32_t fat;
    uint32_t fat_sectors;
    uint32_t data;
    uint32_t clusters;
    uint32_t bitmap_clusters;
    uint32_t root_cluster;
    uint8_t cluster_shift;
    bool exfat;
} CardFormatPlan;

typedef struct CardFormat {
    BlockDevice device;
    CardFormatPlan plan;
    bool (*progress)(void *context, uint32_t done, uint32_t total);
    void *context;
    uint32_t done;
    uint32_t total;
    uint32_t buffer[CARD_FORMAT_BUFFER_SECTORS * CARD_FORMAT_SECTOR_BYTES / 4];
} CardFormat;

bool card_format_plan(CardFormatPlan *plan, uint32_t sectors, uint32_t allocation_unit);
bool card_format_run(CardFormat *format);
