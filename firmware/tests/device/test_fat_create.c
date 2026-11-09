#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/fat.h"
#include "sparse_card.h"

enum {
    SMALL_SIZE = 4096,
    LARGE_SIZE = 4 * 1024 * 1024,
    CLEARED_SIZE = 4096,
    SECTOR_SIZE = 512
};

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

static uint8_t pattern_byte(uint32_t seed, uint32_t index)
{
    return (uint8_t)(index * 31u + seed);
}

static void check_pattern_file(Card *card, const char *path, uint32_t seed, uint32_t size)
{
    FatFile file;
    CHECK(fat_open(&card->volume, path, &file));
    CHECK_EQUAL(size, file.size);
    uint8_t *bytes = malloc(size + 1);
    CHECK(fat_read(&card->volume, &file, 0, bytes, size));
    uint32_t mismatches = 0;
    for (uint32_t index = 0; index < size; index++) {
        mismatches += bytes[index] != pattern_byte(seed, index);
    }
    CHECK_EQUAL(0, mismatches);
    free(bytes);
}

static void check_fixture_files(Card *card)
{
    check_pattern_file(card, "/.existing/library.db", 1, 3000);
    check_pattern_file(card, "/README.TXT", 2, 100);
    check_pattern_file(card, "/notes.txt", 3, 700);
    check_pattern_file(card, INTRO_PATH, 4, 9000);
}

static void check_cleared_file(Card *card, const char *path, uint32_t size)
{
    FatFile file;
    CHECK(fat_open(&card->volume, path, &file));
    CHECK_EQUAL(size, file.size);
    uint32_t cleared = size < CLEARED_SIZE ? size : CLEARED_SIZE;
    uint8_t bytes[CLEARED_SIZE];
    CHECK(fat_read(&card->volume, &file, 0, bytes, cleared));
    uint32_t nonzero = 0;
    for (uint32_t index = 0; index < cleared; index++) {
        nonzero += bytes[index] != 0;
    }
    CHECK_EQUAL(0, nonzero);
}

static void check_contiguous(Card *card, const char *path)
{
    FatFile file;
    CHECK(fat_open(&card->volume, path, &file));
    uint32_t last_sector = (file.size - 1) / SECTOR_SIZE;
    uint32_t first = 0;
    uint32_t last = 0;
    CHECK(fat_file_sector(&card->volume, &file, 0, &first));
    CHECK(fat_file_sector(&card->volume, &file, last_sector, &last));
    CHECK_EQUAL(first + last_sector, last);
}

static void check_write_at_end(Card *card, const char *path)
{
    FatFile file;
    CHECK(fat_open(&card->volume, path, &file));
    uint8_t written[1000];
    for (uint32_t index = 0; index < sizeof written; index++) {
        written[index] = (uint8_t)(0x5A ^ index);
    }
    CHECK(fat_write(&card->volume, &file, file.size - sizeof written, written, sizeof written));
    remount(card);
    CHECK(fat_open(&card->volume, path, &file));
    uint8_t bytes[sizeof written];
    CHECK(fat_read(&card->volume, &file, file.size - sizeof bytes, bytes, sizeof bytes));
    CHECK(memcmp(bytes, written, sizeof bytes) == 0);
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

static void reserve_the_player_files(Card *card)
{
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/.doublesharp/settings.bin", SMALL_SIZE));
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/.doublesharp/favorites.bin", SMALL_SIZE));
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/.doublesharp/library.db", LARGE_SIZE));
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/.existing/covers.bin", LARGE_SIZE));
}

static void check_reserve(const char *name)
{
    Card *card = mounted(name);
    reserve_the_player_files(card);
    remount(card);
    check_cleared_file(card, "/.doublesharp/settings.bin", SMALL_SIZE);
    check_cleared_file(card, "/.doublesharp/favorites.bin", SMALL_SIZE);
    check_cleared_file(card, "/.doublesharp/library.db", LARGE_SIZE);
    check_cleared_file(card, "/.existing/covers.bin", LARGE_SIZE);
    check_contiguous(card, "/.doublesharp/library.db");
    check_contiguous(card, "/.existing/covers.bin");
    CHECK(listed(card, "/", ".doublesharp"));
    CHECK(listed(card, "/.doublesharp", "favorites.bin"));
    CHECK(listed(card, "/.doublesharp", "library.db"));
    CHECK(listed(card, "/.existing", "covers.bin"));
    check_fixture_files(card);
    check_write_at_end(card, "/.doublesharp/library.db");
    check_write_at_end(card, "/.existing/covers.bin");
    check_fixture_files(card);
    save_card(card, name);
    sparse_card_free(&card->sparse);
}

static void fat32_reserves_a_new_folder_and_files_that_read_back_cleared(void)
{
    check_reserve("fat32_mbr");
}

static void fat16_reserves_a_new_folder_and_files_that_read_back_cleared(void)
{
    check_reserve("fat16_superfloppy");
}

static void exfat_reserves_a_new_folder_and_files_that_read_back_cleared(void)
{
    check_reserve("exfat_mbr");
}

static void reserving_an_existing_file_changes_nothing(void)
{
    Card *card = mounted("exfat_mbr");
    CHECK_EQUAL(FAT_RESERVE_EXISTS,
                fat_reserve(&card->volume, "/.existing/library.db", LARGE_SIZE));
    CHECK_EQUAL(FAT_RESERVE_EXISTS, fat_reserve(&card->volume, "/Music", SMALL_SIZE));
    CHECK_EQUAL(0, card->sparse.writes);
    check_pattern_file(card, "/.existing/library.db", 1, 3000);
    sparse_card_free(&card->sparse);
}

static void check_no_space(const char *name)
{
    Card *card = mounted(name);
    uint32_t too_big = 120u * 1024 * 1024;
    CHECK_EQUAL(FAT_RESERVE_NO_SPACE, fat_reserve(&card->volume, "/.existing/huge.bin", too_big));
    CHECK_EQUAL(0, card->sparse.writes);
    FatFile file;
    CHECK(!fat_open(&card->volume, "/.existing/huge.bin", &file));
    sparse_card_free(&card->sparse);
}

static void a_reserve_beyond_the_free_space_fails_and_writes_nothing(void)
{
    check_no_space("fat32_mbr");
    check_no_space("fat16_superfloppy");
    check_no_space("exfat_mbr");
}

static void check_bad_paths(const char *name)
{
    Card *card = mounted(name);
    CHECK_EQUAL(FAT_RESERVE_FAILED, fat_reserve(&card->volume, "/bad:name.bin", SMALL_SIZE));
    CHECK_EQUAL(FAT_RESERVE_FAILED, fat_reserve(&card->volume, "/README.TXT/inside", SMALL_SIZE));
    CHECK_EQUAL(FAT_RESERVE_FAILED, fat_reserve(&card->volume, "/\xc3\xa9t\xc3\xa9.bin", 1));
    CHECK_EQUAL(FAT_RESERVE_FAILED, fat_reserve(&card->volume, "/", SMALL_SIZE));
    CHECK_EQUAL(FAT_RESERVE_FAILED, fat_reserve(&card->volume, "/.existing/empty.bin", 0));
    CHECK_EQUAL(0, card->sparse.writes);
    sparse_card_free(&card->sparse);
}

static void names_the_card_cannot_hold_are_refused_without_writes(void)
{
    check_bad_paths("fat32_mbr");
    check_bad_paths("exfat_mbr");
}

static void long_names_with_the_same_start_get_distinct_short_names(void)
{
    Card *card = mounted("fat16_superfloppy");
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/.doublesharp/favorites one.bin", SMALL_SIZE));
    CHECK_EQUAL(FAT_RESERVE_CREATED,
                fat_reserve(&card->volume, "/.doublesharp/favorites two.bin", 600));
    uint32_t writes = card->sparse.writes;
    CHECK_EQUAL(FAT_RESERVE_FAILED, fat_reserve(&card->volume, "/.doublesharp/FAVORI~1.BIN", 600));
    CHECK_EQUAL(writes, card->sparse.writes);
    remount(card);
    FatFile one;
    FatFile two;
    CHECK(fat_open(&card->volume, "/.doublesharp/favorites one.bin", &one));
    CHECK(fat_open(&card->volume, "/.doublesharp/favorites two.bin", &two));
    CHECK(one.first_cluster != two.first_cluster);
    CHECK_EQUAL(SMALL_SIZE, one.size);
    CHECK_EQUAL(600, two.size);
    CHECK(listed(card, "/.doublesharp", "favorites one.bin"));
    CHECK(listed(card, "/.doublesharp", "favorites two.bin"));
    save_card(card, "fat16_short_names");
    sparse_card_free(&card->sparse);
}

static void check_few_writes(const char *name)
{
    Card *card = mounted(name);
    uint32_t size = 32u * 1024 * 1024;
    uint32_t clusters = size / ((uint32_t)SECTOR_SIZE << card->volume.cluster_shift);
    uint32_t map_sectors = card->volume.type == FAT_TYPE_EXFAT
                               ? clusters / (SECTOR_SIZE * 8) + 2
                               : card->volume.fat_count * (clusters * 4 / SECTOR_SIZE + 2);
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&card->volume, "/.existing/covers.bin", size));
    CHECK(card->sparse.writes <= map_sectors + 16);
    check_cleared_file(card, "/.existing/covers.bin", size);
    sparse_card_free(&card->sparse);
}

static void reserving_32_megabytes_takes_few_writes(void)
{
    check_few_writes("fat32_mbr");
    check_few_writes("exfat_mbr");
}

static void lower_case_short_names_keep_their_case(void)
{
    Card *card = mounted("fat32_mbr");
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&card->volume, "/resume.bin", SMALL_SIZE));
    remount(card);
    CHECK(listed(card, "/", "resume.bin"));
    sparse_card_free(&card->sparse);
}

static void set_info_next_free(Card *card, uint32_t value)
{
    uint8_t *info = card->sparse.bytes + (size_t)card->volume.info_sector * SECTOR_SIZE;
    for (int index = 0; index < 4; index++) {
        info[492 + index] = (uint8_t)(value >> (8 * index));
    }
}

static uint32_t reserved_first_cluster(Card *card, const char *path, uint32_t size)
{
    FatFile file;
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&card->volume, path, size));
    CHECK(fat_open(&card->volume, path, &file));
    return file.first_cluster;
}

static void a_reservation_starts_after_the_previous_one(void)
{
    Card *card = mounted("fat32_mbr");
    uint32_t first = reserved_first_cluster(card, "/.existing/one.bin", SMALL_SIZE);
    uint32_t second = reserved_first_cluster(card, "/.existing/two.bin", SMALL_SIZE);
    CHECK_EQUAL(first + SMALL_SIZE / SECTOR_SIZE, second);
    sparse_card_free(&card->sparse);
}

static void a_reservation_starts_at_the_hint_kept_on_the_volume(void)
{
    Card *card = mounted("fat32_mbr");
    uint32_t hint = card->volume.cluster_count / 2;
    set_info_next_free(card, hint);
    remount(card);
    CHECK_EQUAL(hint, reserved_first_cluster(card, "/.existing/hinted.bin", SMALL_SIZE));
    sparse_card_free(&card->sparse);
}

static void a_hint_too_near_the_end_wraps_around_to_find_room(void)
{
    Card *card = mounted("fat32_mbr");
    set_info_next_free(card, card->volume.cluster_count + 1);
    remount(card);
    uint32_t first = reserved_first_cluster(card, "/.existing/wrapped.bin", SMALL_SIZE);
    CHECK(first < card->volume.cluster_count / 2);
    sparse_card_free(&card->sparse);
}

static void a_hint_that_is_not_a_cluster_is_ignored(void)
{
    uint32_t bad[] = {0, 1, 0xFFFFFFFFu, 0x0FFFFFFFu};
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; index++) {
        Card *card = mounted("fat32_mbr");
        set_info_next_free(card, bad[index]);
        remount(card);
        CHECK(reserved_first_cluster(card, "/.existing/ignored.bin", SMALL_SIZE) >= 2);
        sparse_card_free(&card->sparse);
    }
}

static void a_hint_in_the_middle_of_used_space_still_finds_the_free_run(void)
{
    Card *card = mounted("fat32_mbr");
    FatFile file;
    CHECK(fat_open(&card->volume, "/Many/file number 000.txt", &file));
    set_info_next_free(card, file.first_cluster);
    remount(card);
    uint32_t first = reserved_first_cluster(card, "/.existing/inside.bin", LARGE_SIZE);
    CHECK(first != file.first_cluster);
    check_cleared_file(card, "/.existing/inside.bin", LARGE_SIZE);
    check_fixture_files(card);
    sparse_card_free(&card->sparse);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(fat32_reserves_a_new_folder_and_files_that_read_back_cleared),
        TEST_CASE(fat16_reserves_a_new_folder_and_files_that_read_back_cleared),
        TEST_CASE(exfat_reserves_a_new_folder_and_files_that_read_back_cleared),
        TEST_CASE(reserving_an_existing_file_changes_nothing),
        TEST_CASE(a_reserve_beyond_the_free_space_fails_and_writes_nothing),
        TEST_CASE(names_the_card_cannot_hold_are_refused_without_writes),
        TEST_CASE(long_names_with_the_same_start_get_distinct_short_names),
        TEST_CASE(lower_case_short_names_keep_their_case),
        TEST_CASE(reserving_32_megabytes_takes_few_writes),
        TEST_CASE(a_reservation_starts_after_the_previous_one),
        TEST_CASE(a_reservation_starts_at_the_hint_kept_on_the_volume),
        TEST_CASE(a_hint_too_near_the_end_wraps_around_to_find_room),
        TEST_CASE(a_hint_that_is_not_a_cluster_is_ignored),
        TEST_CASE(a_hint_in_the_middle_of_used_space_still_finds_the_free_run),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
