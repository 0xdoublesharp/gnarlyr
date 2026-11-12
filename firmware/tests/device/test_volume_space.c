#include <string.h>

#include "check.h"
#include "device/fat.h"
#include "device/volume_space.h"
#include "sparse_card.h"

enum {
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
        TEST_CASE(reserving_a_file_lowers_both_counts),
    };
    int failed = test_run_all(cases, TEST_CASE_COUNT(cases));
    sparse_card_free(&card);
    return failed;
}
