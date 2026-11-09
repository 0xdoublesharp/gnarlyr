#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/fat.h"
#include "sparse_card.h"

enum {
    CLUSTERS = 70000,
    RESERVED = 32,
    FAT_SECTORS = (CLUSTERS + 2) * 4 / 512 + 1,
    DATA_START = RESERVED + FAT_SECTORS,
    FILE_ENTRY = 0,
    ROOT_CLUSTER = 2,
    FIRST_FILE_CLUSTER = 10,
    BIG_CLUSTERS = 3000,
    FRAGMENTS = 100
};

typedef struct Run {
    uint32_t start;
    uint32_t length;
} Run;

typedef struct Generated {
    uint32_t *fat;
    uint32_t fat_reads;
    uint32_t file_size;
    uint32_t first;
} Generated;

static void put32(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static void boot_sector(uint8_t *sector)
{
    sector[0] = 0xEB;
    sector[11] = 0x00;
    sector[12] = 0x02;
    sector[13] = 1;
    sector[14] = RESERVED;
    sector[16] = 1;
    put32(sector + 32, DATA_START + CLUSTERS);
    put32(sector + 36, FAT_SECTORS);
    put32(sector + 44, ROOT_CLUSTER);
    sector[510] = 0x55;
    sector[511] = 0xAA;
}

static void root_sector(const Generated *generated, uint8_t *sector)
{
    memcpy(sector, "BIG     BIN", 11);
    sector[11] = 0x20;
    sector[20] = (uint8_t)(generated->first >> 16);
    sector[21] = (uint8_t)(generated->first >> 24);
    sector[26] = (uint8_t)generated->first;
    sector[27] = (uint8_t)(generated->first >> 8);
    put32(sector + 28, generated->file_size);
}

static void fill_sector(Generated *generated, uint32_t lba, uint8_t *sector)
{
    memset(sector, 0, 512);
    if (lba == 0) {
        boot_sector(sector);
    } else if (lba >= RESERVED && lba < DATA_START) {
        generated->fat_reads++;
        for (uint32_t word = 0; word < 128; word++) {
            uint32_t cluster = (lba - RESERVED) * 128 + word;
            put32(sector + word * 4, cluster < CLUSTERS + 2 ? generated->fat[cluster] : 0);
        }
    } else if (lba == DATA_START) {
        root_sector(generated, sector);
    } else {
        for (uint32_t word = 0; word < 128; word++) {
            put32(sector + word * 4, lba);
        }
    }
}

static bool read_generated(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    Generated *generated = context;
    for (uint32_t index = 0; index < count; index++) {
        fill_sector(generated, lba + index, (uint8_t *)buffer + index * 512);
    }
    return true;
}

static void lay_out(Generated *generated, const Run *runs, uint32_t count, uint32_t last_next)
{
    generated->fat = calloc(CLUSTERS + 2, sizeof(uint32_t));
    generated->fat[ROOT_CLUSTER] = 0x0FFFFFFF;
    generated->first = runs[0].start;
    uint32_t total = 0;
    for (uint32_t run = 0; run < count; run++) {
        for (uint32_t step = 0; step < runs[run].length; step++) {
            uint32_t cluster = runs[run].start + step;
            bool last_of_run = step + 1 == runs[run].length;
            generated->fat[cluster] = !last_of_run      ? cluster + 1
                                      : run + 1 < count ? runs[run + 1].start
                                                        : last_next;
            total++;
        }
    }
    generated->file_size = total * 512 - 100;
}

static void open_generated(Generated *generated, FatVolume *volume, FatFile *file,
                           FatExtents *extents)
{
    BlockDevice device = {read_generated, generated, NULL};
    CHECK(fat_mount(volume, device));
    CHECK(fat_open(volume, "/BIG.BIN", file));
    if (extents != NULL) {
        fat_attach_extents(file, extents);
    }
}

static uint32_t expected_lba(const Run *runs, uint32_t count, uint32_t index)
{
    for (uint32_t run = 0; run < count; run++) {
        if (index < runs[run].length) {
            return DATA_START + runs[run].start + index - 2;
        }
        index -= runs[run].length;
    }
    return 0;
}

static bool sector_is(FatVolume *volume, FatFile *file, uint32_t index, uint32_t lba)
{
    uint8_t word[4];
    if (!fat_read(volume, file, index * 512, word, 4)) {
        return false;
    }
    return (uint32_t)word[0] | (uint32_t)word[1] << 8 | (uint32_t)word[2] << 16 |
           (uint32_t)word[3] << 24 == lba;
}

static uint32_t mismatches(FatVolume *volume, FatFile *file, const Run *runs, uint32_t count,
                           uint32_t clusters)
{
    uint32_t wrong = 0;
    for (uint32_t step = 0; step < clusters; step++) {
        uint32_t index = (step * 7919u) % clusters;
        wrong += !sector_is(volume, file, index, expected_lba(runs, count, index));
    }
    return wrong;
}

static void a_contiguous_file_maps_to_one_run_and_far_seeks_read_no_more_fat(void)
{
    Generated generated = {0};
    Run runs[] = {{FIRST_FILE_CLUSTER, BIG_CLUSTERS}};
    lay_out(&generated, runs, 1, 0x0FFFFFFF);
    FatVolume volume;
    FatFile file;
    FatExtents extents;
    open_generated(&generated, &volume, &file, &extents);
    CHECK(sector_is(&volume, &file, 2500, expected_lba(runs, 1, 2500)));
    CHECK_EQUAL(FAT_EXTENTS_READY, extents.state);
    CHECK_EQUAL(1, extents.count);
    uint32_t reads = generated.fat_reads;
    CHECK_EQUAL(0, mismatches(&volume, &file, runs, 1, BIG_CLUSTERS - 1));
    CHECK_EQUAL(reads, generated.fat_reads);
    free(generated.fat);
}

static void a_fragmented_file_maps_every_run_and_seeks_backward_and_forward(void)
{
    Generated generated = {0};
    Run runs[] = {{20, 500}, {5000, 300}, {900, 700}, {40000, 1}, {3000, 900}, {60000, 600}};
    lay_out(&generated, runs, 6, 0x0FFFFFFF);
    FatVolume volume;
    FatFile file;
    FatExtents extents;
    open_generated(&generated, &volume, &file, &extents);
    CHECK(sector_is(&volume, &file, 2999, expected_lba(runs, 6, 2999)));
    CHECK_EQUAL(FAT_EXTENTS_READY, extents.state);
    CHECK_EQUAL(6, extents.count);
    CHECK_EQUAL(0, extents.stride);
    uint32_t reads = generated.fat_reads;
    CHECK_EQUAL(0, mismatches(&volume, &file, runs, 6, 3000));
    CHECK_EQUAL(reads, generated.fat_reads);
    free(generated.fat);
}

static void a_file_with_too_many_runs_keeps_checkpoints_and_walks_a_short_distance(void)
{
    Generated generated = {0};
    static Run runs[FRAGMENTS];
    for (uint32_t run = 0; run < FRAGMENTS; run++) {
        runs[run] = (Run){.start = 100 + (FRAGMENTS - 1 - run) * 40, .length = 30};
    }
    lay_out(&generated, runs, FRAGMENTS, 0x0FFFFFFF);
    FatVolume volume;
    FatFile file;
    FatExtents extents;
    open_generated(&generated, &volume, &file, &extents);
    CHECK(sector_is(&volume, &file, 2999, expected_lba(runs, FRAGMENTS, 2999)));
    CHECK_EQUAL(FAT_EXTENTS_READY, extents.state);
    CHECK(extents.stride > 0);
    CHECK(extents.count <= FAT_EXTENT_CAPACITY);
    CHECK_EQUAL(0, mismatches(&volume, &file, runs, FRAGMENTS, 3000));
    uint32_t before = generated.fat_reads;
    CHECK(sector_is(&volume, &file, 5, expected_lba(runs, FRAGMENTS, 5)));
    CHECK(sector_is(&volume, &file, 2990, expected_lba(runs, FRAGMENTS, 2990)));
    CHECK(generated.fat_reads - before <= 4);
    free(generated.fat);
}

static void reading_sequentially_from_an_unaligned_offset_walks_the_chain_once(void)
{
    Generated generated = {0};
    static Run runs[FRAGMENTS];
    for (uint32_t run = 0; run < FRAGMENTS; run++) {
        runs[run] = (Run){.start = 100 + run * 40, .length = 30};
    }
    lay_out(&generated, runs, FRAGMENTS, 0x0FFFFFFF);
    FatVolume volume;
    FatFile file;
    open_generated(&generated, &volume, &file, NULL);
    static uint8_t bytes[8192];
    uint32_t reads_before = 0;
    for (uint32_t offset = 3 * 512 + 100; offset + sizeof bytes < generated.file_size;
         offset += sizeof bytes) {
        CHECK(fat_read(&volume, &file, offset, bytes, sizeof bytes));
        reads_before = generated.fat_reads;
    }
    CHECK(reads_before <= 130);
    free(generated.fat);
}

static void sequential_reads_deep_in_a_file_read_no_more_fat_than_at_its_start(void)
{
    Generated generated = {0};
    Run runs[] = {{FIRST_FILE_CLUSTER, 6000}};
    lay_out(&generated, runs, 1, 0x0FFFFFFF);
    FatVolume volume;
    FatFile file;
    open_generated(&generated, &volume, &file, NULL);
    static uint8_t bytes[8192];
    uint32_t offset = 4000 * 512 + 100;
    CHECK(fat_read(&volume, &file, offset, bytes, sizeof bytes));
    uint32_t before = generated.fat_reads;
    for (int step = 1; step <= 100; step++) {
        CHECK(
            fat_read(&volume, &file, offset + (uint32_t)step * sizeof bytes, bytes, sizeof bytes));
    }
    CHECK(generated.fat_reads - before <= 40);
    free(generated.fat);
}

static void a_broken_chain_fails_the_read_without_a_map(void)
{
    Generated generated = {0};
    Run runs[] = {{20, 500}};
    lay_out(&generated, runs, 1, 0);
    generated.fat[300] = 0;
    FatVolume volume;
    FatFile file;
    FatExtents extents;
    open_generated(&generated, &volume, &file, &extents);
    uint8_t byte;
    CHECK(!fat_read(&volume, &file, 450 * 512, &byte, 1));
    CHECK_EQUAL(FAT_EXTENTS_FAILED, extents.state);
    CHECK(fat_read(&volume, &file, 10 * 512, &byte, 1));
    free(generated.fat);
}

static void an_exfat_file_seeks_far_and_back_without_a_map(void)
{
    SparseCard sparse;
    CHECK(sparse_card_load(&sparse, "exfat_mbr"));
    FatVolume volume;
    FatFile file;
    FatExtents extents;
    uint8_t bytes[16];
    CHECK(fat_mount(&volume, sparse_card_device(&sparse)));
    CHECK(fat_open(&volume, "/.echo/library.db", &file));
    fat_attach_extents(&file, &extents);
    CHECK(fat_read(&volume, &file, 2900, bytes, 16));
    CHECK(fat_read(&volume, &file, 0, bytes, 16));
    CHECK(fat_read(&volume, &file, 2500, bytes, 16));
    CHECK(extents.state != FAT_EXTENTS_FAILED);
    sparse_card_free(&sparse);
}

static void a_directory_gets_no_map(void)
{
    SparseCard sparse;
    CHECK(sparse_card_load(&sparse, "fat32_mbr"));
    FatVolume volume;
    FatDirCursor cursor;
    FatExtents extents;
    CHECK(fat_mount(&volume, sparse_card_device(&sparse)));
    CHECK(fat_open_directory(&volume, "/Music", &cursor));
    fat_attach_extents(&cursor.directory, &extents);
    CHECK(cursor.directory.extents == NULL);
    sparse_card_free(&sparse);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_contiguous_file_maps_to_one_run_and_far_seeks_read_no_more_fat),
        TEST_CASE(a_fragmented_file_maps_every_run_and_seeks_backward_and_forward),
        TEST_CASE(a_file_with_too_many_runs_keeps_checkpoints_and_walks_a_short_distance),
        TEST_CASE(reading_sequentially_from_an_unaligned_offset_walks_the_chain_once),
        TEST_CASE(sequential_reads_deep_in_a_file_read_no_more_fat_than_at_its_start),
        TEST_CASE(a_broken_chain_fails_the_read_without_a_map),
        TEST_CASE(an_exfat_file_seeks_far_and_back_without_a_map),
        TEST_CASE(a_directory_gets_no_map),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
