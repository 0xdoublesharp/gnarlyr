#include "device/card_format.h"

#include <stddef.h>

#include "card_format_upcase.h"
#include "core/bytes.h"

static uint32_t rounded(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

static uint32_t common_alignment(uint32_t first, uint32_t second)
{
    uint32_t a = first;
    uint32_t b = second;
    while (b != 0) {
        uint32_t next = a % b;
        a = b;
        b = next;
    }
    return first / a * second;
}

bool card_format_plan(CardFormatPlan *plan, uint32_t sectors, uint32_t allocation_unit)
{
    if (sectors < 131072 || allocation_unit > 131072) {
        return false;
    }
    bool exfat = sectors > CARD_FORMAT_SDHC_SECTORS;
    uint32_t boundary = exfat ? 32768 : 8192;
    uint32_t alignment =
        allocation_unit == 0 ? boundary : common_alignment(boundary, allocation_unit);
    if (!exfat && alignment > 32768) {
        return false;
    }
    *plan = (CardFormatPlan){.sectors = sectors,
                             .alignment = alignment,
                             .start = alignment,
                             .exfat = exfat,
                             .cluster_shift = exfat                ? 8
                                              : sectors <= 4194304 ? 5
                                                                   : 6};
    while ((sectors >> plan->cluster_shift) < 131072 && plan->cluster_shift > 0) {
        plan->cluster_shift--;
    }
    uint32_t fat_sectors = ((sectors >> plan->cluster_shift) + 2 + 127) / 128;
    plan->fat_sectors = fat_sectors;
    plan->data = rounded(plan->start + (exfat ? alignment : 32) + fat_sectors * (exfat ? 1u : 2u),
                         alignment);
    plan->fat = exfat ? plan->start + alignment : plan->data - 2 * fat_sectors;
    plan->clusters = (sectors - plan->data) >> plan->cluster_shift;
    plan->bitmap_clusters = exfat ? rounded((plan->clusters + 7) / 8, 512u << 8) / (512u << 8) : 0;
    plan->root_cluster = exfat ? 3 + plan->bitmap_clusters : 2;
    return plan->data < sectors && (exfat || plan->fat - plan->start <= UINT16_MAX) &&
           (exfat || plan->clusters >= 65525);
}

static uint8_t *buffer(CardFormat *format)
{
    return (uint8_t *)format->buffer;
}

static void clear(CardFormat *format)
{
    for (size_t index = 0; index < sizeof format->buffer / sizeof format->buffer[0]; index++) {
        format->buffer[index] = 0;
    }
}

static void copy_text(uint8_t *bytes, const char *text, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        bytes[index] = (uint8_t)text[index];
    }
}

static bool write_buffer(CardFormat *format, uint32_t lba, uint32_t count)
{
    if (lba >= format->plan.sectors || count > format->plan.sectors - lba ||
        (format->progress != NULL &&
         !format->progress(format->context, format->done, format->total))) {
        return false;
    }
    if (!format->device.write(format->device.context, lba, count, format->buffer)) {
        return false;
    }
    format->done += count;
    return true;
}

static bool zero_sectors(CardFormat *format, uint32_t lba, uint32_t count)
{
    clear(format);
    while (count > 0) {
        uint32_t run = count < CARD_FORMAT_BUFFER_SECTORS ? count : CARD_FORMAT_BUFFER_SECTORS;
        if (!write_buffer(format, lba, run)) {
            return false;
        }
        lba += run;
        count -= run;
    }
    return true;
}

static void fat32_boot(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    uint8_t *bytes = buffer(format);
    clear(format);
    bytes[0] = 0xEB;
    bytes[1] = 0x58;
    bytes[2] = 0x90;
    copy_text(bytes + 3, "MSDOS5.0", 8);
    bytes_put_le16(bytes + 11, 512);
    bytes[13] = (uint8_t)(1u << plan->cluster_shift);
    bytes_put_le16(bytes + 14, plan->fat - plan->start);
    bytes[16] = 2;
    bytes[21] = 0xF8;
    bytes_put_le16(bytes + 24, 63);
    bytes_put_le16(bytes + 26, 255);
    bytes_put_le32(bytes + 28, plan->start);
    bytes_put_le32(bytes + 32, plan->sectors - plan->start);
    bytes_put_le32(bytes + 36, plan->fat_sectors);
    bytes_put_le32(bytes + 44, 2);
    bytes_put_le16(bytes + 48, 1);
    bytes_put_le16(bytes + 50, 6);
    bytes[64] = 0x80;
    bytes[66] = 0x29;
    bytes_put_le32(bytes + 67, 0x4543484Fu ^ plan->sectors);
    copy_text(bytes + 71, CARD_FORMAT_LABEL_PADDED, 11);
    copy_text(bytes + 82, "FAT32   ", 8);
    bytes_put_le16(bytes + 510, 0xAA55);
}

static bool fat32_system(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    if (!zero_sectors(format, plan->start,
                      plan->data - plan->start + (1u << plan->cluster_shift))) {
        return false;
    }
    fat32_boot(format);
    if (!write_buffer(format, plan->start, 1) || !write_buffer(format, plan->start + 6, 1)) {
        return false;
    }
    clear(format);
    uint8_t *bytes = buffer(format);
    bytes_put_le32(bytes, 0x41615252);
    bytes_put_le32(bytes + 484, 0x61417272);
    bytes_put_le32(bytes + 488, plan->clusters - 1);
    bytes_put_le32(bytes + 492, 3);
    bytes_put_le32(bytes + 508, 0xAA550000);
    if (!write_buffer(format, plan->start + 1, 1) || !write_buffer(format, plan->start + 7, 1)) {
        return false;
    }
    clear(format);
    bytes_put_le32(bytes, 0x0FFFFFF8);
    bytes_put_le32(bytes + 4, 0xFFFFFFFF);
    bytes_put_le32(bytes + 8, 0x0FFFFFFF);
    if (!write_buffer(format, plan->fat, 1) ||
        !write_buffer(format, plan->fat + plan->fat_sectors, 1)) {
        return false;
    }
    clear(format);
    copy_text(bytes, CARD_FORMAT_LABEL_PADDED, 11);
    bytes[11] = 8;
    return write_buffer(format, plan->data, 1);
}

static uint32_t checksum_bytes(uint32_t checksum, const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        checksum = (checksum >> 1 | checksum << 31) + bytes[index];
    }
    return checksum;
}

static void exfat_boot(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    clear(format);
    uint8_t *bytes = buffer(format);
    bytes[0] = 0xEB;
    bytes[1] = 0x76;
    bytes[2] = 0x90;
    copy_text(bytes + 3, "EXFAT   ", 8);
    bytes_put_le32(bytes + 64, plan->start);
    bytes_put_le32(bytes + 72, plan->sectors - plan->start);
    bytes_put_le32(bytes + 80, plan->fat - plan->start);
    bytes_put_le32(bytes + 84, plan->fat_sectors);
    bytes_put_le32(bytes + 88, plan->data - plan->start);
    bytes_put_le32(bytes + 92, plan->clusters);
    bytes_put_le32(bytes + 96, plan->root_cluster);
    bytes_put_le32(bytes + 100, 0x4543484Fu ^ plan->sectors);
    bytes_put_le16(bytes + 104, 0x100);
    bytes[108] = 9;
    bytes[109] = plan->cluster_shift;
    bytes[110] = 1;
    bytes[111] = 0x80;
    bytes_put_le16(bytes + 510, 0xAA55);
}

static bool exfat_boot_region(CardFormat *format, uint32_t start)
{
    exfat_boot(format);
    uint8_t *bytes = buffer(format);
    uint32_t checksum = 0;
    for (uint32_t index = 0; index < 512; index++) {
        if (index != 106 && index != 107 && index != 112) {
            checksum = checksum_bytes(checksum, bytes + index, 1);
        }
    }
    if (!write_buffer(format, start, 1)) {
        return false;
    }
    for (uint32_t index = 1; index <= 10; index++) {
        clear(format);
        if (index <= 8) {
            bytes_put_le32(bytes + 508, 0xAA550000);
        }
        checksum = checksum_bytes(checksum, bytes, 512);
        if (!write_buffer(format, start + index, 1)) {
            return false;
        }
    }
    for (uint32_t index = 0; index < 512; index += 4) {
        bytes_put_le32(bytes + index, checksum);
    }
    return write_buffer(format, start + 11, 1);
}

static bool exfat_fat(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    if (!zero_sectors(format, plan->fat, plan->fat_sectors)) {
        return false;
    }
    clear(format);
    uint8_t *bytes = buffer(format);
    bytes_put_le32(bytes, 0xFFFFFFF8);
    bytes_put_le32(bytes + 4, 0xFFFFFFFF);
    for (uint32_t cluster = 2; cluster <= plan->root_cluster; cluster++) {
        bytes_put_le32(bytes + 4 * cluster,
                       cluster < plan->bitmap_clusters + 1 ? cluster + 1 : 0xFFFFFFFF);
    }
    return write_buffer(format, plan->fat, 1);
}

static bool exfat_bitmap(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    uint32_t used = plan->bitmap_clusters + 2;
    uint32_t sectors = used << plan->cluster_shift;
    if (!zero_sectors(format, plan->data, sectors)) {
        return false;
    }
    clear(format);
    uint8_t *bytes = buffer(format);
    for (uint32_t bit = 0; bit < used; bit++) {
        bytes[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
    if (!write_buffer(format, plan->data, 1)) {
        return false;
    }
    uint32_t upcase = plan->data + (plan->bitmap_clusters << plan->cluster_shift);
    clear(format);
    for (size_t offset = 0; offset < sizeof CARD_FORMAT_UPCASE; offset += sizeof format->buffer) {
        clear(format);
        uint32_t length = (uint32_t)(sizeof CARD_FORMAT_UPCASE - offset);
        length = length < sizeof format->buffer ? length : sizeof format->buffer;
        for (uint32_t index = 0; index < length; index++) {
            bytes[index] = CARD_FORMAT_UPCASE[offset + index];
        }
        if (!write_buffer(format, upcase + (uint32_t)offset / 512, (length + 511) / 512)) {
            return false;
        }
    }
    return true;
}

static bool exfat_root(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    clear(format);
    uint8_t *bytes = buffer(format);
    bytes[0] = 0x83;
    bytes[1] = 8;
    for (uint32_t index = 0; index < 8; index++) {
        bytes_put_le16(bytes + 2 + 2 * index, (uint8_t)CARD_FORMAT_LABEL[index]);
    }
    bytes[32] = 0x81;
    bytes_put_le32(bytes + 32 + 20, 2);
    bytes_put_le32(bytes + 32 + 24, (plan->clusters + 7) / 8);
    bytes[64] = 0x82;
    bytes_put_le32(bytes + 64 + 4, 0xE619D30D);
    bytes_put_le32(bytes + 64 + 20, 2 + plan->bitmap_clusters);
    bytes_put_le32(bytes + 64 + 24, sizeof CARD_FORMAT_UPCASE);
    return write_buffer(format, plan->data + ((plan->root_cluster - 2) << plan->cluster_shift), 1);
}

static bool exfat_system(CardFormat *format)
{
    return exfat_fat(format) && exfat_bitmap(format) && exfat_root(format) &&
           exfat_boot_region(format, format->plan.start) &&
           exfat_boot_region(format, format->plan.start + 12);
}

static bool publish_partition(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    clear(format);
    uint8_t *bytes = buffer(format);
    uint8_t *entry = bytes + 446;
    entry[1] = 0xFE;
    entry[2] = 0xFF;
    entry[3] = 0xFF;
    entry[4] = plan->exfat ? 7 : 0x0C;
    entry[5] = 0xFE;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    bytes_put_le32(entry + 8, plan->start);
    bytes_put_le32(entry + 12, plan->sectors - plan->start);
    bytes_put_le16(bytes + 510, 0xAA55);
    return write_buffer(format, 0, 1);
}

static bool same_plan(const CardFormatPlan *first, const CardFormatPlan *second)
{
    return first->sectors == second->sectors && first->alignment == second->alignment &&
           first->start == second->start && first->fat == second->fat &&
           first->fat_sectors == second->fat_sectors && first->data == second->data &&
           first->clusters == second->clusters &&
           first->bitmap_clusters == second->bitmap_clusters &&
           first->root_cluster == second->root_cluster &&
           first->cluster_shift == second->cluster_shift && first->exfat == second->exfat;
}

bool card_format_run(CardFormat *format)
{
    const CardFormatPlan *plan = &format->plan;
    CardFormatPlan checked;
    if (format->device.write == NULL ||
        !card_format_plan(&checked, plan->sectors, plan->alignment) || !same_plan(&checked, plan)) {
        return false;
    }
    uint32_t root_sectors = 1u << plan->cluster_shift;
    format->done = 0;
    format->total = plan->exfat
                        ? 2 + plan->fat_sectors + 1 + (plan->bitmap_clusters + 2) * root_sectors +
                              1 + (sizeof CARD_FORMAT_UPCASE + 511) / 512 + 1 + 24
                        : 2 + plan->data - plan->start + root_sectors + 7;
    bool made = zero_sectors(format, 0, 1) &&
                (plan->exfat ? exfat_system(format) : fat32_system(format)) &&
                publish_partition(format);
    if (made && format->progress != NULL) {
        format->progress(format->context, format->done, format->total);
    }
    return made;
}
