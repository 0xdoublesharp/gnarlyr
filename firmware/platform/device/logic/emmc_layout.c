#include "device/emmc_layout.h"
#include "core/bytes.h"

#define IDB_MAGIC 0xFCDC8C3Bu

enum {
    SECTOR_SIZE = 512,
    IDB_FIRST = 0x40,
    IDB_STRIDE = 0x400,
    IDB_COPIES = 5,
    IDB_SYSTEM_MIB = 0x202,
    IDB_TAG = 0x20A,
    FIRMWARE_START = 0x2000,
    SECTORS_PER_MIB = 0x800,
    SCAN_LIMIT = FIRMWARE_START + 0x100000,
    SIGNATURE = 0x1FE,
    PARTITION_ENTRY = 0x1BE,
    HEADER_MAGIC = 0x1F8,
    HEADER_FW_SIZE = 0x1F4,
    HEADER_BYTES = 0x200,
    SEARCH_MARGIN = 0x104000,
    SEARCH_TRIES = 20
};

static bool matches(const uint8_t *bytes, const char *text, int length)
{
    for (int index = 0; index < length; index++) {
        if (bytes[index] != (uint8_t)text[index]) {
            return false;
        }
    }
    return true;
}

static bool read_system_mib(const BlockDevice *device, uint32_t *system_mib, uint32_t *scratch)
{
    uint32_t *words = scratch;
    const uint8_t *bytes = (const uint8_t *)words;
    for (uint32_t copy = 0; copy < IDB_COPIES; copy++) {
        uint32_t lba = IDB_FIRST + copy * IDB_STRIDE;
        if (device->read(device->context, lba, 2, words) && bytes_le32(bytes) == IDB_MAGIC &&
            matches(bytes + IDB_TAG, "NANO", 4) && bytes_le16(bytes + IDB_SYSTEM_MIB) != 0) {
            *system_mib = bytes_le16(bytes + IDB_SYSTEM_MIB);
            return true;
        }
    }
    return false;
}

static bool read_header(const BlockDevice *device, uint32_t lba, uint32_t *words)
{
    return device->read(device->context, lba, 1, words) &&
           matches((const uint8_t *)words + HEADER_MAGIC, "RKnanoFW", 8);
}

static bool header_copy_matches(const BlockDevice *device, uint32_t lba, const uint32_t *header,
                                uint32_t *copy)
{
    uint32_t size = bytes_le32((const uint8_t *)header + HEADER_FW_SIZE);
    if (size < HEADER_BYTES || size % SECTOR_SIZE != 0 ||
        !device->read(device->context, lba + size / SECTOR_SIZE, 1, copy)) {
        return false;
    }
    for (uint32_t index = 0; index < SECTOR_SIZE / 4; index++) {
        if (copy[index] != header[index]) {
            return false;
        }
    }
    return true;
}

static bool first_slot_header(const BlockDevice *device, uint32_t *words, uint32_t *copy)
{
    return read_header(device, FIRMWARE_START, words) &&
           header_copy_matches(device, FIRMWARE_START, words, copy);
}

static bool searched_slot_2(const BlockDevice *device, uint32_t half, uint32_t *slot,
                            uint32_t *scratch)
{
    uint32_t *first = scratch;
    uint32_t *candidate = scratch + SECTOR_SIZE / 4;
    uint32_t *copy = scratch + 2 * SECTOR_SIZE / 4;
    if (!first_slot_header(device, first, copy)) {
        return false;
    }
    uint32_t size = bytes_le32((const uint8_t *)first + HEADER_FW_SIZE);
    uint32_t start = ((size + SEARCH_MARGIN) / SECTOR_SIZE) & ~(uint32_t)(SECTORS_PER_MIB - 1);
    for (uint32_t step = 0; step < SEARCH_TRIES; step++) {
        uint32_t offset = start + step * SECTORS_PER_MIB;
        if (offset > half) {
            return false;
        }
        uint32_t lba = FIRMWARE_START + offset;
        if (read_header(device, lba, candidate)) {
            *slot = lba;
            return header_copy_matches(device, lba, candidate, copy);
        }
    }
    return false;
}

static bool is_volume_start(const uint8_t *sector)
{
    if (bytes_le16(sector + SIGNATURE) != 0xAA55) {
        return false;
    }
    const uint8_t *entry = sector + PARTITION_ENTRY;
    bool exfat = matches(sector + 3, "EXFAT   ", 8);
    bool fat = (sector[0] == 0xEB || sector[0] == 0xE9) && bytes_le16(sector + 11) == SECTOR_SIZE;
    bool mbr =
        (entry[0] == 0x00 || entry[0] == 0x80) && entry[4] != 0 && bytes_le32(entry + 8) != 0;
    return exfat || fat || mbr;
}

static bool find_volume(const BlockDevice *device, uint32_t first, uint32_t total, uint32_t *start,
                        uint32_t *words)
{
    for (uint32_t lba = first; lba < SCAN_LIMIT && lba < total; lba += SECTORS_PER_MIB) {
        if (device->read(device->context, lba, 1, words) &&
            is_volume_start((const uint8_t *)words)) {
            *start = lba;
            return true;
        }
    }
    return false;
}

static bool inside(uint32_t start, uint32_t length, uint32_t lba, uint32_t count)
{
    return lba >= start && lba - start < length && count <= length - (lba - start);
}

bool emmc_layout_in_volume(const EmmcLayout *layout, uint32_t lba, uint32_t count)
{
    return lba < layout->volume_sectors && count <= layout->volume_sectors - lba;
}

bool emmc_layout_allows_write(const EmmcLayout *layout, uint32_t lba, uint32_t count)
{
    return count > 0 && (inside(layout->volume_start, layout->volume_sectors, lba, count) ||
                         inside(layout->slot_1, layout->slot_sectors, lba, count) ||
                         inside(layout->slot_2, layout->slot_sectors, lba, count));
}

bool emmc_layout_read(EmmcLayout *layout, const BlockDevice *device, uint32_t total_sectors)
{
    static uint32_t scratch[EMMC_LAYOUT_SCRATCH_WORDS];
    return emmc_layout_read_with(layout, device, total_sectors,
                                 (EmmcLayoutRead){EMMC_SLOT_2_HALF, scratch});
}

bool emmc_layout_read_with(EmmcLayout *layout, const BlockDevice *device, uint32_t total_sectors,
                           EmmcLayoutRead read)
{
    uint32_t system_mib = 0;
    if (!read_system_mib(device, &system_mib, read.scratch)) {
        return false;
    }
    uint32_t firmware_sectors = system_mib * SECTORS_PER_MIB;
    *layout = (EmmcLayout){
        .slot_1 = FIRMWARE_START,
        .slot_2 = FIRMWARE_START + firmware_sectors / 2,
        .slot_sectors = firmware_sectors / 2,
        .firmware_end = FIRMWARE_START + firmware_sectors,
    };
    if (read.placement == EMMC_SLOT_2_SEARCH) {
        uint32_t found = 0;
        if (searched_slot_2(device, layout->slot_sectors, &found, read.scratch)) {
            layout->slot_2 = found;
        }
    }
    if (!find_volume(device, layout->firmware_end, total_sectors, &layout->volume_start,
                     read.scratch)) {
        return false;
    }
    layout->volume_sectors = total_sectors - layout->volume_start;
    return true;
}
