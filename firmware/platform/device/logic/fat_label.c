#include "fat_layout.h"

enum {
    BOOT_SIGNATURE = 0xAA55,
    SIGNATURE_OFFSET = 510,
    DIRECTORY_ENTRY_SIZE = 32,
    LABEL_LENGTH = 11,
    LABEL_ROOT_LIMIT = 65536,
    ENTRIES_PER_SECTOR = FAT_SECTOR_SIZE / DIRECTORY_ENTRY_SIZE,
    EXFAT_LABEL_ENTRY = 0x83,
    DELETED_ENTRY = 0xE5,
    ATTRIBUTE_OFFSET = 11,
    ATTRIBUTE_VOLUME = 0x08,
    ATTRIBUTE_DIRECTORY = 0x10,
    ATTRIBUTE_LONG_NAME_MASK = 0x3F,
    ATTRIBUTE_LONG_NAME = 0x0F,
    EXTENDED_SIGNATURE = 0x29,
    EXTENDED_SIGNATURE_BEFORE_LABEL = 5,
    FAT16_BOOT_LABEL = 43,
    FAT32_BOOT_LABEL = 71,
    RESERVED_SECTORS_OFFSET = 14,
    BACKUP_BOOT_OFFSET = 50
};

typedef struct LabelRequest {
    const char *target;
    const char *legacy;
    bool exfat;
    unsigned boot_label;
} LabelRequest;

static uint8_t label_fold(uint8_t value)
{
    return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

static bool label_matches(const uint8_t *entry, const char *name, bool exfat)
{
    unsigned length = 0;
    while (name[length] != 0) {
        length++;
    }
    if (exfat && entry[1] != length) {
        return false;
    }
    for (unsigned index = 0; index < LABEL_LENGTH; index++) {
        uint8_t expected = index < length ? (uint8_t)name[index] : ' ';
        if (exfat && index >= length) {
            break;
        }
        unsigned offset = exfat ? 2 + index * 2 : index;
        if (label_fold(entry[offset]) != label_fold(expected) ||
            (exfat && entry[offset + 1] != 0)) {
            return false;
        }
    }
    return true;
}

static bool label_wanted(const uint8_t *label, const LabelRequest *request, bool exfat)
{
    return label_matches(label, request->target, exfat) ||
           (request->legacy != NULL && label_matches(label, request->legacy, exfat));
}

static bool is_label_entry(const uint8_t *entry, bool exfat)
{
    if (exfat) {
        return entry[0] == EXFAT_LABEL_ENTRY;
    }
    uint8_t attributes = entry[ATTRIBUTE_OFFSET];
    return entry[0] != DELETED_ENTRY &&
           (attributes & ATTRIBUTE_LONG_NAME_MASK) != ATTRIBUTE_LONG_NAME &&
           (attributes & (ATTRIBUTE_VOLUME | ATTRIBUTE_DIRECTORY)) == ATTRIBUTE_VOLUME;
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right, unsigned length)
{
    for (unsigned index = 0; index < length; index++) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static void label_bytes(uint8_t *desired, const char *target, bool exfat)
{
    unsigned length = 0;
    while (target[length] != 0 && length < LABEL_LENGTH) {
        length++;
    }
    if (exfat) {
        desired[1] = (uint8_t)length;
    }
    for (unsigned byte = 0; byte < LABEL_LENGTH; byte++) {
        unsigned offset = exfat ? 2 + byte * 2 : byte;
        desired[offset] = byte < length ? (uint8_t)target[byte] : exfat ? 0 : ' ';
        if (exfat) {
            desired[offset + 1] = 0;
        }
    }
}

static bool fix_root_label(FatVolume *volume, FatFile *root, uint32_t index,
                           const LabelRequest *request)
{
    const uint8_t *entry = fat_read_entry(volume, root, index);
    if (entry == NULL) {
        return false;
    }
    uint8_t desired[DIRECTORY_ENTRY_SIZE];
    for (unsigned byte = 0; byte < DIRECTORY_ENTRY_SIZE; byte++) {
        desired[byte] = entry[byte];
    }
    label_bytes(desired, request->target, request->exfat);
    uint32_t lba;
    return bytes_equal(desired, entry, DIRECTORY_ENTRY_SIZE) ||
           (fat_file_sector(volume, root, index / ENTRIES_PER_SECTOR, &lba) &&
            fat_update_bytes(volume, lba, index % ENTRIES_PER_SECTOR * DIRECTORY_ENTRY_SIZE,
                             desired, DIRECTORY_ENTRY_SIZE));
}

static bool fix_boot_label(FatVolume *volume, uint32_t lba, const uint8_t *header,
                           const LabelRequest *request)
{
    unsigned label = request->boot_label;
    if (!fat_read_sector(volume, lba)) {
        return false;
    }
    const uint8_t *boot = fat_cached_sector(volume);
    if (fat_le16(boot + SIGNATURE_OFFSET) != BOOT_SIGNATURE || !bytes_equal(boot, header, label)) {
        return false;
    }
    if (boot[label - EXTENDED_SIGNATURE_BEFORE_LABEL] != EXTENDED_SIGNATURE ||
        !label_wanted(boot + label, request, false)) {
        return true;
    }
    uint8_t desired[LABEL_LENGTH];
    label_bytes(desired, request->target, false);
    return bytes_equal(boot + label, desired, LABEL_LENGTH) ||
           fat_update_bytes(volume, lba, label, desired, LABEL_LENGTH);
}

static bool fix_boot_labels(FatVolume *volume, const LabelRequest *request)
{
    if (request->boot_label == 0) {
        return true;
    }
    if (!fat_read_sector(volume, volume->volume_start)) {
        return false;
    }
    uint8_t header[FAT32_BOOT_LABEL];
    for (unsigned byte = 0; byte < request->boot_label; byte++) {
        header[byte] = fat_cached_sector(volume)[byte];
    }
    bool fat32 = volume->type == FAT_TYPE_32;
    uint32_t backup = fat32 ? fat_le16(header + BACKUP_BOOT_OFFSET) : 0;
    uint32_t reserved = fat_le16(header + RESERVED_SECTORS_OFFSET);
    return fix_boot_label(volume, volume->volume_start, header, request) &&
           (backup == 0 || backup >= reserved ||
            fix_boot_label(volume, volume->volume_start + backup, header, request));
}

static unsigned boot_label_offset(FatType type)
{
    if (type == FAT_TYPE_EXFAT) {
        return 0;
    }
    return type == FAT_TYPE_32 ? FAT32_BOOT_LABEL : FAT16_BOOT_LABEL;
}

bool fat_fix_label(FatVolume *volume, const char *target, const char *legacy)
{
    bool exfat = volume->type == FAT_TYPE_EXFAT;
    LabelRequest request = {target, legacy, exfat, boot_label_offset(volume->type)};
    FatFile root = fat_root_directory(volume);
    for (uint32_t index = 0; index < LABEL_ROOT_LIMIT; index++) {
        const uint8_t *entry = fat_read_entry(volume, &root, index);
        if (entry == NULL || entry[0] == 0) {
            return fix_boot_labels(volume, &request);
        }
        if (is_label_entry(entry, exfat)) {
            return !label_wanted(entry, &request, exfat) ||
                   (fix_root_label(volume, &root, index, &request) &&
                    fix_boot_labels(volume, &request));
        }
    }
    return false;
}
