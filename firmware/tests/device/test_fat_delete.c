#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/fat.h"
#include "sparse_card.h"

enum {
    SECTOR_SIZE = 512,
    FAT32_EOC = 0x0FFFFFFF,
    CHAINED_CLUSTERS = 4,
    INFO_FREE_OFFSET = 488,
    ENTRY_SIZE = 32
};

#define EXFAT_EOC 0xFFFFFFFFu

static const char README_PATH[] = "/README.TXT";
static const char LONG_FOLDER[] = "/Music/Sigur R\xc3\xb3s/\xc3\x81g\xc3\xa6tis byrjun";
static const char LONG_PATH[] = "/Music/Sigur R\xc3\xb3s/\xc3\x81g\xc3\xa6tis byrjun/"
                                "02 Svefn-g-englar with a long name.flac";
static const char INTRO_PATH[] =
    "/Music/Sigur R\xc3\xb3s/\xc3\x81g\xc3\xa6tis byrjun/01 Intro.flac";

typedef struct Card {
    SparseCard sparse;
    FatVolume volume;
} Card;

static Card *mounted(const char *name)
{
    static Card card;
    CHECK(sparse_card_load(&card.sparse, name));
    CHECK(fat_mount(&card.volume, sparse_card_device(&card.sparse)));
    return &card;
}

static void remount(Card *card)
{
    CHECK(fat_mount(&card->volume, sparse_card_device(&card->sparse)));
}

static bool listed(Card *card, const char *folder, const char *name)
{
    FatDirCursor cursor;
    static FatDirEntry entry;
    CHECK(fat_open_directory(&card->volume, folder, &cursor));
    while (fat_next_entry(&card->volume, &cursor, &entry)) {
        if (strcmp(entry.name, name) == 0) {
            return true;
        }
    }
    return false;
}

static uint32_t count_listed(Card *card, const char *folder)
{
    FatDirCursor cursor;
    static FatDirEntry entry;
    uint32_t count = 0;
    CHECK(fat_open_directory(&card->volume, folder, &cursor));
    while (fat_next_entry(&card->volume, &cursor, &entry)) {
        count++;
    }
    return count;
}

static void check_pattern(Card *card, const char *path, uint32_t seed, uint32_t size)
{
    FatFile file;
    CHECK(fat_open(&card->volume, path, &file));
    CHECK_EQUAL(size, file.size);
    uint8_t *bytes = malloc(size + 1);
    CHECK(fat_read(&card->volume, &file, 0, bytes, size));
    uint32_t mismatches = 0;
    for (uint32_t index = 0; index < size; index++) {
        mismatches += bytes[index] != (uint8_t)(index * 31u + seed);
    }
    CHECK_EQUAL(0, mismatches);
    free(bytes);
}

static void check_survivors(Card *card)
{
    check_pattern(card, "/.echo/library.db", 1, 3000);
    check_pattern(card, "/notes.txt", 3, 700);
    check_pattern(card, INTRO_PATH, 4, 9000);
}

static void save_card(Card *card, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.img", ECHO_CREATED_DIR, name);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    size_t start = (size_t)card->volume.volume_start * SECTOR_SIZE;
    size_t length = (size_t)card->sparse.sector_count * SECTOR_SIZE - start;
    CHECK_EQUAL(length, fwrite(card->sparse.bytes + start, 1, length, file));
    fclose(file);
}

static uint8_t *sector_bytes(Card *card, uint32_t lba)
{
    return card->sparse.bytes + (size_t)lba * SECTOR_SIZE;
}

static uint32_t info_free_count(Card *card)
{
    const uint8_t *info = sector_bytes(card, card->volume.info_sector);
    return (uint32_t)info[INFO_FREE_OFFSET] | (uint32_t)info[INFO_FREE_OFFSET + 1] << 8 |
           (uint32_t)info[INFO_FREE_OFFSET + 2] << 16 | (uint32_t)info[INFO_FREE_OFFSET + 3] << 24;
}

static uint32_t clusters_of(Card *card, uint32_t size)
{
    uint32_t cluster_bytes = (uint32_t)SECTOR_SIZE << card->volume.cluster_shift;
    return (size + cluster_bytes - 1) / cluster_bytes;
}

static void check_delete_everywhere(const char *name)
{
    Card *card = mounted(name);
    uint32_t many = count_listed(card, "/Many");
    CHECK(fat_delete(&card->volume, README_PATH));
    CHECK(fat_delete(&card->volume, LONG_PATH));
    CHECK(fat_delete(&card->volume, "/Music/Empty.wav"));
    CHECK(fat_delete(&card->volume, "/Many/file number 010.txt"));
    remount(card);
    FatFile file;
    CHECK(!fat_open(&card->volume, README_PATH, &file));
    CHECK(!fat_open(&card->volume, LONG_PATH, &file));
    CHECK(!fat_open(&card->volume, "/Many/file number 010.txt", &file));
    CHECK(!listed(card, "/", "README.TXT"));
    CHECK(!listed(card, LONG_FOLDER, "02 Svefn-g-englar with a long name.flac"));
    CHECK(listed(card, LONG_FOLDER, "01 Intro.flac"));
    CHECK_EQUAL(many - 1, count_listed(card, "/Many"));
    CHECK(listed(card, "/Many", "file number 009.txt"));
    CHECK(listed(card, "/Many", "file number 011.txt"));
    check_survivors(card);
    char image[64];
    snprintf(image, sizeof image, "%s_deleted", name);
    save_card(card, image);
    sparse_card_free(&card->sparse);
}

static void fat32_deleted_files_vanish_and_the_volume_stays_consistent(void)
{
    check_delete_everywhere("fat32_mbr");
}

static void fat16_deleted_files_vanish_and_the_volume_stays_consistent(void)
{
    check_delete_everywhere("fat16_superfloppy");
}

static void exfat_deleted_files_vanish_and_the_volume_stays_consistent(void)
{
    check_delete_everywhere("exfat_mbr");
}

static void deleting_something_that_is_not_a_file_changes_nothing(void)
{
    Card *card = mounted("fat32_mbr");
    CHECK(!fat_delete(&card->volume, "/Music"));
    CHECK(!fat_delete(&card->volume, "/missing.bin"));
    CHECK(!fat_delete(&card->volume, "/Nowhere/README.TXT"));
    CHECK(!fat_delete(&card->volume, "/"));
    CHECK(!fat_delete(&card->volume, ""));
    CHECK(!fat_delete(&card->volume, "/Music/"));
    CHECK_EQUAL(0, card->sparse.writes);
    CHECK(fat_delete(&card->volume, "/readme.txt"));
    CHECK(!fat_delete(&card->volume, README_PATH));
    sparse_card_free(&card->sparse);
}

static void a_card_without_write_access_refuses_to_delete(void)
{
    Card *card = mounted("fat32_mbr");
    card->volume.device.write = NULL;
    CHECK(!fat_delete(&card->volume, README_PATH));
    CHECK_EQUAL(0, card->sparse.writes);
    sparse_card_free(&card->sparse);
}

static void deleting_raises_the_free_count_by_the_clusters_of_the_file(void)
{
    Card *card = mounted("fat32_mbr");
    uint32_t before = info_free_count(card);
    CHECK(fat_delete(&card->volume, LONG_PATH));
    CHECK_EQUAL(before + clusters_of(card, 20000), info_free_count(card));
    before = info_free_count(card);
    CHECK(fat_delete(&card->volume, "/Music/Empty.wav"));
    CHECK_EQUAL(before, info_free_count(card));
    sparse_card_free(&card->sparse);
}

static void deleting_writes_every_copy_of_the_table(void)
{
    Card *card = mounted("fat32_mbr");
    FatFile file;
    CHECK(fat_open(&card->volume, LONG_PATH, &file));
    uint32_t first = file.first_cluster;
    CHECK(fat_delete(&card->volume, LONG_PATH));
    for (uint32_t copy = 0; copy < card->volume.fat_count; copy++) {
        uint32_t lba = card->volume.fat_start + copy * card->volume.fat_sectors + first / 128;
        const uint8_t *entry = sector_bytes(card, lba) + first % 128 * 4;
        CHECK_EQUAL(0, entry[0] | entry[1] | entry[2] | (entry[3] & 0x0F));
    }
    sparse_card_free(&card->sparse);
}

static void a_deleted_files_space_is_reused_by_the_next_reservation(void)
{
    Card *card = mounted("fat32_mbr");
    FatFile file;
    CHECK(fat_open(&card->volume, LONG_PATH, &file));
    uint32_t first = file.first_cluster;
    CHECK(fat_delete(&card->volume, LONG_PATH));
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&card->volume, "/.echo/again.bin", 20000));
    CHECK(fat_open(&card->volume, "/.echo/again.bin", &file));
    CHECK_EQUAL(first, file.first_cluster);
    check_survivors(card);
    sparse_card_free(&card->sparse);
}

static void set_fat32_entry(Card *card, uint32_t cluster, uint32_t value)
{
    for (uint32_t copy = 0; copy < card->volume.fat_count; copy++) {
        uint32_t lba = card->volume.fat_start + copy * card->volume.fat_sectors + cluster / 128;
        uint8_t *entry = sector_bytes(card, lba) + cluster % 128 * 4;
        for (int index = 0; index < 4; index++) {
            entry[index] = (uint8_t)(value >> (8 * index));
        }
    }
}

static uint32_t fat32_entry(Card *card, uint32_t cluster)
{
    uint32_t lba = card->volume.fat_start + cluster / 128;
    const uint8_t *entry = sector_bytes(card, lba) + cluster % 128 * 4;
    return ((uint32_t)entry[0] | (uint32_t)entry[1] << 8 | (uint32_t)entry[2] << 16 |
            (uint32_t)entry[3] << 24) &
           FAT32_EOC;
}

static void a_fragmented_chain_is_freed_across_table_sectors(void)
{
    Card *card = mounted("fat32_mbr");
    uint32_t size = 130 * SECTOR_SIZE;
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&card->volume, "/.echo/chain.bin", size));
    FatFile file;
    CHECK(fat_open(&card->volume, "/.echo/chain.bin", &file));
    uint32_t first = file.first_cluster;
    uint32_t last = first + 129;
    set_fat32_entry(card, first, last);
    set_fat32_entry(card, last, first + 1);
    set_fat32_entry(card, first + 1, first + 2);
    set_fat32_entry(card, first + 128, FAT32_EOC);
    set_fat32_entry(card, first + 2, first + 128);
    uint32_t before = info_free_count(card);
    remount(card);
    CHECK(fat_delete(&card->volume, "/.echo/chain.bin"));
    CHECK_EQUAL(before + 5, info_free_count(card));
    uint32_t members[] = {first, last, first + 1, first + 2, first + 128};
    for (size_t index = 0; index < sizeof members / sizeof members[0]; index++) {
        CHECK_EQUAL(0, fat32_entry(card, members[index]));
    }
    CHECK(fat32_entry(card, first + 3) != 0);
    sparse_card_free(&card->sparse);
}

static uint8_t *exfat_stream_of(Card *card, uint32_t first_cluster, uint8_t **primary)
{
    uint32_t cluster_sectors = 1u << card->volume.cluster_shift;
    uint32_t root = card->volume.data_start + (card->volume.root_cluster - 2) * cluster_sectors;
    uint8_t *bytes = sector_bytes(card, root);
    for (uint32_t offset = 0; offset < cluster_sectors * SECTOR_SIZE; offset += ENTRY_SIZE) {
        uint32_t cluster = (uint32_t)bytes[offset + 20] | (uint32_t)bytes[offset + 21] << 8 |
                           (uint32_t)bytes[offset + 22] << 16 | (uint32_t)bytes[offset + 23] << 24;
        if (bytes[offset] == 0xC0 && cluster == first_cluster) {
            *primary = bytes + offset - ENTRY_SIZE;
            return bytes + offset;
        }
    }
    return NULL;
}

static void refresh_set_checksum(uint8_t *primary)
{
    uint16_t checksum = 0;
    uint32_t length = (1u + primary[1]) * ENTRY_SIZE;
    for (uint32_t index = 0; index < length; index++) {
        if (index != 2 && index != 3) {
            checksum = (uint16_t)((((checksum & 1u) != 0 ? 0x8000u : 0u) + (checksum >> 1) +
                                   primary[index]) &
                                  0xFFFF);
        }
    }
    primary[2] = (uint8_t)checksum;
    primary[3] = (uint8_t)(checksum >> 8);
}

static void set_exfat_entry(Card *card, uint32_t cluster, uint32_t value)
{
    uint32_t lba = card->volume.fat_start + cluster / 128;
    uint8_t *entry = sector_bytes(card, lba) + cluster % 128 * 4;
    for (int index = 0; index < 4; index++) {
        entry[index] = (uint8_t)(value >> (8 * index));
    }
}

static bool exfat_bit(Card *card, uint32_t cluster)
{
    uint32_t cluster_sectors = 1u << card->volume.cluster_shift;
    uint32_t root = card->volume.data_start + (card->volume.root_cluster - 2) * cluster_sectors;
    const uint8_t *bytes = sector_bytes(card, root);
    for (uint32_t offset = 0; offset < cluster_sectors * SECTOR_SIZE; offset += ENTRY_SIZE) {
        if (bytes[offset] == 0x81) {
            uint32_t first = (uint32_t)bytes[offset + 20] | (uint32_t)bytes[offset + 21] << 8;
            uint32_t bit = cluster - 2;
            uint32_t lba = card->volume.data_start + (first - 2) * cluster_sectors + bit / 4096;
            return (sector_bytes(card, lba)[bit % 4096 / 8] >> (bit % 8) & 1) != 0;
        }
    }
    CHECK(false);
    return false;
}

static void check_exfat_chained_file(const uint32_t order[CHAINED_CLUSTERS])
{
    Card *card = mounted("exfat_mbr");
    uint32_t cluster_bytes = (uint32_t)SECTOR_SIZE << card->volume.cluster_shift;
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/chained.bin", CHAINED_CLUSTERS * cluster_bytes - 100));
    FatFile file;
    CHECK(fat_open(&card->volume, "/chained.bin", &file));
    uint32_t first = file.first_cluster;
    uint8_t *primary = NULL;
    uint8_t *stream = exfat_stream_of(card, first, &primary);
    CHECK(stream != NULL);
    stream[1] &= (uint8_t)~0x02;
    refresh_set_checksum(primary);
    for (uint32_t index = 0; index < CHAINED_CLUSTERS; index++) {
        set_exfat_entry(card, first + order[index],
                        index + 1 < CHAINED_CLUSTERS ? first + order[index + 1] : EXFAT_EOC);
        CHECK(exfat_bit(card, first + index));
    }
    remount(card);
    CHECK(fat_open(&card->volume, "/chained.bin", &file));
    CHECK(!file.contiguous);
    CHECK(fat_delete(&card->volume, "/chained.bin"));
    for (uint32_t index = 0; index < CHAINED_CLUSTERS; index++) {
        CHECK(!exfat_bit(card, first + index));
    }
    remount(card);
    CHECK(!fat_open(&card->volume, "/chained.bin", &file));
    check_survivors(card);
    sparse_card_free(&card->sparse);
}

static void an_exfat_file_with_a_table_chain_releases_every_cluster(void)
{
    static const uint32_t in_order[CHAINED_CLUSTERS] = {0, 1, 2, 3};
    static const uint32_t shuffled[CHAINED_CLUSTERS] = {0, 2, 1, 3};
    check_exfat_chained_file(in_order);
    check_exfat_chained_file(shuffled);
}

static void an_exfat_delete_keeps_the_neighbouring_allocations(void)
{
    Card *card = mounted("exfat_mbr");
    FatFile before;
    FatFile after;
    CHECK(fat_open(&card->volume, "/notes.txt", &before));
    CHECK(fat_delete(&card->volume, README_PATH));
    CHECK(fat_open(&card->volume, "/notes.txt", &after));
    CHECK(exfat_bit(card, before.first_cluster));
    CHECK_EQUAL(before.first_cluster, after.first_cluster);
    sparse_card_free(&card->sparse);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(fat32_deleted_files_vanish_and_the_volume_stays_consistent),
        TEST_CASE(fat16_deleted_files_vanish_and_the_volume_stays_consistent),
        TEST_CASE(exfat_deleted_files_vanish_and_the_volume_stays_consistent),
        TEST_CASE(deleting_something_that_is_not_a_file_changes_nothing),
        TEST_CASE(a_card_without_write_access_refuses_to_delete),
        TEST_CASE(deleting_raises_the_free_count_by_the_clusters_of_the_file),
        TEST_CASE(deleting_writes_every_copy_of_the_table),
        TEST_CASE(a_deleted_files_space_is_reused_by_the_next_reservation),
        TEST_CASE(a_fragmented_chain_is_freed_across_table_sectors),
        TEST_CASE(an_exfat_file_with_a_table_chain_releases_every_cluster),
        TEST_CASE(an_exfat_delete_keeps_the_neighbouring_allocations),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
