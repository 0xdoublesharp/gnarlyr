#include <stdbool.h>

#include "check.h"
#include "device/storage_mount.h"
#include "sparse_card.h"

static SparseCard card_disk;
static SparseCard internal_disk;
static bool card_present;
static bool internal_present;
static FatVolume volume;

static bool start_card(BlockDevice *device)
{
    *device = sparse_card_device(&card_disk);
    return card_present;
}

static bool start_internal(BlockDevice *device)
{
    *device = sparse_card_device(&internal_disk);
    return internal_present;
}

static void prepare(bool card, bool internal)
{
    sparse_card_free(&card_disk);
    sparse_card_free(&internal_disk);
    CHECK(sparse_card_load(&card_disk, "fat32_mbr"));
    CHECK(sparse_card_load(&internal_disk, "fat16_superfloppy"));
    card_present = card;
    internal_present = internal;
}

static void a_readable_card_is_mounted_first(void)
{
    prepare(true, true);
    CHECK_EQUAL(STORAGE_CARD, storage_mount(&volume, start_card, start_internal));
    CHECK_EQUAL(FAT_TYPE_32, volume.type);
}

static void without_a_card_the_internal_volume_is_mounted(void)
{
    prepare(false, true);
    CHECK_EQUAL(STORAGE_INTERNAL, storage_mount(&volume, start_card, start_internal));
    CHECK_EQUAL(FAT_TYPE_16, volume.type);
}

static void a_card_that_does_not_mount_falls_back_to_internal(void)
{
    prepare(true, true);
    for (uint32_t index = 0; index < 1024; index++) {
        card_disk.bytes[index] = 0;
    }
    CHECK_EQUAL(STORAGE_INTERNAL, storage_mount(&volume, start_card, start_internal));
}

static void with_neither_nothing_is_mounted(void)
{
    prepare(false, false);
    CHECK_EQUAL(STORAGE_NONE, storage_mount(&volume, start_card, start_internal));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_readable_card_is_mounted_first),
        TEST_CASE(without_a_card_the_internal_volume_is_mounted),
        TEST_CASE(a_card_that_does_not_mount_falls_back_to_internal),
        TEST_CASE(with_neither_nothing_is_mounted),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
