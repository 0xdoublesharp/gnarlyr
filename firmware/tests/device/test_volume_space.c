#include <string.h>

#include "check.h"
#include "device/fat.h"
#include "device/volume_space.h"
#include "sparse_card.h"

enum {
    SECTOR_BYTES = 512,
    EXFAT_FREE_SECTORS = 128424,
    FAT32_FREE_SECTORS = 127606,
    FAT16_FREE_SECTORS = 30948,
    PARTITION_START = 63,
    INFO_FREE_AT = 488,
    RESERVED_BYTES = 1024 * 1024,
    RESERVED_SECTORS = RESERVED_BYTES / 512,
    BUFFER_SECTORS = 8
};

static uint32_t words[BUFFER_SECTORS * 512 / 4];
static SparseCard card;
static FatVolume volume;

typedef struct FailingReader {
    BlockDevice delegate;
    uint32_t fail_at;
    uint32_t calls;
} FailingReader;

static bool failing_read(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    FailingReader *reader = context;
    if (reader->calls++ == reader->fail_at) {
        return false;
    }
    return reader->delegate.read(reader->delegate.context, lba, count, buffer);
}

static uint32_t free_with(BlockDevice device, uint32_t sectors)
{
    return volume_space_free_sectors(device, (VolumeSpaceBuffer){words, sectors});
}

static void put16(uint8_t *bytes, uint32_t at, uint16_t value)
{
    bytes[at] = (uint8_t)value;
    bytes[at + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t at, uint32_t value)
{
    put16(bytes, at, (uint16_t)value);
    put16(bytes, at + 2, (uint16_t)(value >> 16));
}

static uint32_t get32(const uint8_t *bytes, uint32_t at)
{
    return (uint32_t)bytes[at] | (uint32_t)bytes[at + 1] << 8 | (uint32_t)bytes[at + 2] << 16 |
           (uint32_t)bytes[at + 3] << 24;
}

static uint32_t free_on(const char *name, uint32_t sectors)
{
    sparse_card_free(&card);
    CHECK(sparse_card_load(&card, name));
    return volume_space_free_sectors(sparse_card_device(&card),
                                     (VolumeSpaceBuffer){words, sectors});
}

static uint32_t free_again(void)
{
    return volume_space_free_sectors(sparse_card_device(&card),
                                     (VolumeSpaceBuffer){words, BUFFER_SECTORS});
}

static void exfat_free_space_counts_the_allocation_bitmap(void)
{
    CHECK_EQUAL(EXFAT_FREE_SECTORS, free_on("exfat_mbr", BUFFER_SECTORS));
}

static void a_one_sector_buffer_counts_the_same_bitmap(void)
{
    CHECK_EQUAL(EXFAT_FREE_SECTORS, free_on("exfat_mbr", 1));
}

static void fat32_free_space_comes_from_fsinfo(void)
{
    CHECK_EQUAL(FAT32_FREE_SECTORS, free_on("fat32_mbr", BUFFER_SECTORS));
}

static void an_unset_or_missing_fsinfo_count_is_unknown(void)
{
    free_on("fat32_mbr", BUFFER_SECTORS);
    memset(card.bytes + (PARTITION_START + 1) * 512 + INFO_FREE_AT, 0xFF, 4);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());
    free_on("fat32_mbr", BUFFER_SECTORS);
    memset(card.bytes + (PARTITION_START + 1) * 512, 0, 4);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());
}

static void fat16_free_space_counts_the_free_table_entries(void)
{
    CHECK_EQUAL(FAT16_FREE_SECTORS, free_on("fat16_superfloppy", BUFFER_SECTORS));
    CHECK_EQUAL(FAT16_FREE_SECTORS, free_on("fat16_superfloppy", 1));
}

static void a_blank_card_is_unknown(void)
{
    free_on("fat16_superfloppy", BUFFER_SECTORS);
    memset(card.bytes, 0, (size_t)card.sector_count * 512);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());
}

static void invalid_buffers_and_every_required_read_are_rejected(void)
{
    free_on("fat16_superfloppy", BUFFER_SECTORS);
    BlockDevice device = sparse_card_device(&card);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN,
                volume_space_free_sectors(device, (VolumeSpaceBuffer){NULL, BUFFER_SECTORS}));
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN,
                volume_space_free_sectors(device, (VolumeSpaceBuffer){words, 0}));

    static const char *const names[] = {"fat16_superfloppy", "fat32_mbr", "exfat_mbr"};
    for (size_t name = 0; name < sizeof names / sizeof names[0]; name++) {
        free_on(names[name], BUFFER_SECTORS);
        card.calls = 0;
        CHECK(free_again() != VOLUME_SPACE_UNKNOWN);
        uint32_t required = card.calls;
        for (uint32_t fail = 0; fail < required; fail++) {
            FailingReader reader = {sparse_card_device(&card), fail, 0};
            BlockDevice failing = {failing_read, &reader, NULL};
            CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_with(failing, BUFFER_SECTORS));
        }
    }
}

static void malformed_fat_metadata_is_unknown(void)
{
    free_on("fat16_superfloppy", BUFFER_SECTORS);
    card.bytes[511] = 0;
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("fat16_superfloppy", BUFFER_SECTORS);
    put16(card.bytes, 11, 1024);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("fat16_superfloppy", BUFFER_SECTORS);
    card.bytes[13] = 0;
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("fat16_superfloppy", BUFFER_SECTORS);
    put16(card.bytes, 22, 0);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("fat16_superfloppy", BUFFER_SECTORS);
    put16(card.bytes, 22, 1);
    put16(card.bytes, 19, UINT16_MAX);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("fat32_mbr", BUFFER_SECTORS);
    uint8_t *boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    put16(boot, 48, 0);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("fat32_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    put32(boot, 32, 0);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());
}

static uint8_t *exfat_bitmap_entry(uint8_t *boot)
{
    uint32_t heap = get32(boot, 88);
    uint32_t root = get32(boot, 96);
    uint32_t lba = PARTITION_START + heap + ((root - 2) << boot[109]);
    uint8_t *directory = card.bytes + (size_t)lba * SECTOR_BYTES;
    for (uint32_t at = 0; at < SECTOR_BYTES; at += 32) {
        if (directory[at] == 0x81 && (directory[at + 1] & 1u) == 0) {
            return directory + at;
        }
    }
    return NULL;
}

static void malformed_exfat_metadata_is_unknown(void)
{
    free_on("exfat_mbr", BUFFER_SECTORS);
    uint8_t *boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    boot[109] = 26;
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    put32(boot, 96, 1);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    put32(boot, 92, VOLUME_SPACE_BITMAP_SECTORS * SECTOR_BYTES * 8u + 1u);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    uint8_t *entry = exfat_bitmap_entry(boot);
    CHECK(entry != NULL);
    entry[0] = 0;
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    entry = exfat_bitmap_entry(boot);
    CHECK(entry != NULL);
    entry[1] = 1;
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    entry = exfat_bitmap_entry(boot);
    CHECK(entry != NULL);
    put32(entry, 20, 1);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    entry = exfat_bitmap_entry(boot);
    CHECK(entry != NULL);
    put32(entry, 28, 1);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());

    free_on("exfat_mbr", BUFFER_SECTORS);
    boot = card.bytes + PARTITION_START * SECTOR_BYTES;
    entry = exfat_bitmap_entry(boot);
    CHECK(entry != NULL);
    put32(entry, 24, 0);
    CHECK_EQUAL(VOLUME_SPACE_UNKNOWN, free_again());
}

static void a_reserved_file_takes_its_space(const char *name, uint32_t before)
{
    CHECK_EQUAL(before, free_on(name, BUFFER_SECTORS));
    CHECK(fat_mount(&volume, sparse_card_device(&card)));
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&volume, "/space.bin", RESERVED_BYTES));
    CHECK_EQUAL(before - RESERVED_SECTORS, free_again());
}

static void reserving_a_file_lowers_both_counts(void)
{
    a_reserved_file_takes_its_space("exfat_mbr", EXFAT_FREE_SECTORS);
    a_reserved_file_takes_its_space("fat32_mbr", FAT32_FREE_SECTORS);
    a_reserved_file_takes_its_space("fat16_superfloppy", FAT16_FREE_SECTORS);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(exfat_free_space_counts_the_allocation_bitmap),
        TEST_CASE(a_one_sector_buffer_counts_the_same_bitmap),
        TEST_CASE(fat32_free_space_comes_from_fsinfo),
        TEST_CASE(an_unset_or_missing_fsinfo_count_is_unknown),
        TEST_CASE(fat16_free_space_counts_the_free_table_entries),
        TEST_CASE(a_blank_card_is_unknown),
        TEST_CASE(invalid_buffers_and_every_required_read_are_rejected),
        TEST_CASE(malformed_fat_metadata_is_unknown),
        TEST_CASE(malformed_exfat_metadata_is_unknown),
        TEST_CASE(reserving_a_file_lowers_both_counts),
    };
    int failed = test_run_all(cases, TEST_CASE_COUNT(cases));
    sparse_card_free(&card);
    return failed;
}
