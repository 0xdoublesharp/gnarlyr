#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "device/card_format.h"
#include "device/fat.h"

typedef struct Image {
    FILE *file;
    uint32_t sectors;
    uint32_t written;
    uint32_t calls;
    uint32_t fail_after;
    uint32_t last_progress;
    uint32_t progress_total;
    uint32_t cancel_after;
} Image;

static CardFormat format;
static FatVolume volume;

static bool read_image(void *context, uint32_t lba, uint32_t count, void *bytes)
{
    Image *image = context;
    CHECK(lba < image->sectors && count <= image->sectors - lba);
    return fseeko(image->file, (off_t)lba * 512, SEEK_SET) == 0 &&
           fread(bytes, 512, count, image->file) == count;
}

static bool write_image(void *context, uint32_t lba, uint32_t count, const void *bytes)
{
    Image *image = context;
    CHECK(lba < image->sectors && count <= image->sectors - lba);
    CHECK(count <= CARD_FORMAT_BUFFER_SECTORS);
    if (++image->calls == image->fail_after) {
        return false;
    }
    image->written += count;
    return fseeko(image->file, (off_t)lba * 512, SEEK_SET) == 0 &&
           fwrite(bytes, 512, count, image->file) == count;
}

static bool progress(void *context, uint32_t done, uint32_t total)
{
    Image *image = context;
    CHECK(done >= image->last_progress && done <= total);
    CHECK(total != 0);
    image->last_progress = done;
    image->progress_total = total;
    return image->cancel_after == 0 || done < image->cancel_after;
}

static bool open_image(Image *image, const char *path, uint32_t sectors)
{
    *image = (Image){.sectors = sectors, .file = fopen(path, "w+b")};
    if (image->file == NULL || ftruncate(fileno(image->file), (off_t)sectors * 512) != 0) {
        return false;
    }
    format = (CardFormat){
        .device = {read_image, image, write_image}, .progress = progress, .context = image};
    return true;
}

static bool format_image(const char *path, uint32_t sectors)
{
    Image image;
    CHECK(open_image(&image, path, sectors));
    bool planned = card_format_plan(&format.plan, sectors, 0);
    CHECK(planned);
    bool made = planned && card_format_run(&format);
    CHECK(made);
    if (made) {
        CHECK(fat_mount(&volume, format.device));
        CHECK_EQUAL(sectors > CARD_FORMAT_SDHC_SECTORS ? FAT_TYPE_EXFAT : FAT_TYPE_32, volume.type);
        CHECK_EQUAL(0, volume.data_start % format.plan.alignment);
        CHECK_EQUAL(0, volume.volume_start % format.plan.alignment);
        FatDirCursor cursor;
        FatDirEntry entry;
        CHECK(fat_open_directory(&volume, "/", &cursor));
        CHECK(!fat_next_entry(&volume, &cursor, &entry));
        CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&volume, "/copied.wav", 4096));
        FatFile file;
        CHECK(fat_open(&volume, "/copied.wav", &file));
        uint8_t bytes[4096];
        memset(bytes, 0x5A, sizeof bytes);
        CHECK(fat_write(&volume, &file, 0, bytes, sizeof bytes));
        memset(bytes, 0, sizeof bytes);
        CHECK(fat_read(&volume, &file, 0, bytes, sizeof bytes));
        CHECK_EQUAL(0x5A, bytes[sizeof bytes - 1]);
        CHECK(image.written < 65536);
        CHECK_EQUAL(image.progress_total, image.last_progress);
    }
    fclose(image.file);
    return made;
}

static void the_sd_capacity_boundary_chooses_fat32_then_exfat(void)
{
    static const uint32_t sizes[] = {2, 16, 32, 64, 256};
    for (size_t index = 0; index < sizeof sizes / sizeof sizes[0]; index++) {
        format_image(ECHO_FORMAT_IMAGE, sizes[index] * 2 * 1024 * 1024);
        CHECK_EQUAL(0, unlink(ECHO_FORMAT_IMAGE));
    }
}

static void unsupported_capacities_and_geometry_are_refused_before_writing(void)
{
    CardFormatPlan plan;
    CHECK(!card_format_plan(&plan, 0, 0));
    CHECK(!card_format_plan(&plan, 1024, 0));
    CHECK(!card_format_plan(&plan, 64 * 1024 * 1024, UINT32_MAX));
}

static void allocation_units_align_both_the_partition_and_the_data(void)
{
    static const uint32_t units[] = {8192, 24576, 32768, 49152, 131072};
    for (size_t index = 0; index < sizeof units / sizeof units[0]; index++) {
        CardFormatPlan plan;
        bool planned = card_format_plan(&plan, 256 * 2 * 1024 * 1024, units[index]);
        CHECK(planned);
        if (planned) {
            CHECK_EQUAL(0, plan.start % units[index]);
            CHECK_EQUAL(0, plan.data % units[index]);
            CHECK_EQUAL(8, plan.cluster_shift);
        }
    }
}

static void check_interrupted(uint32_t sectors)
{
    Image image;
    CHECK(open_image(&image, ECHO_FORMAT_IMAGE, sectors));
    CHECK(card_format_plan(&format.plan, image.sectors, 8192));
    CHECK(card_format_run(&format));
    uint32_t last = image.calls;
    fclose(image.file);
    uint32_t stops[] = {2, 3, 10, 100, last - 2, last};
    for (size_t index = 0; index < sizeof stops / sizeof stops[0]; index++) {
        CHECK(open_image(&image, ECHO_FORMAT_IMAGE, sectors));
        CHECK(card_format_plan(&format.plan, sectors, 8192));
        CHECK(card_format_run(&format));
        image.calls = 0;
        image.last_progress = 0;
        image.fail_after = stops[index];
        CHECK(!card_format_run(&format));
        CHECK(!fat_mount(&volume, format.device));
        CHECK_EQUAL(stops[index], image.calls);
        fclose(image.file);
    }
    CHECK_EQUAL(0, unlink(ECHO_FORMAT_IMAGE));
}

static void interrupted_formats_never_publish_a_mountable_partition(void)
{
    check_interrupted(2 * 2 * 1024 * 1024);
    check_interrupted(64 * 2 * 1024 * 1024);
}

static void cancellation_leaves_the_card_unmountable_and_a_retry_succeeds(void)
{
    Image image;
    CHECK(open_image(&image, ECHO_FORMAT_IMAGE, 64 * 2 * 1024 * 1024));
    CHECK(card_format_plan(&format.plan, image.sectors, 0));
    image.cancel_after = 16;
    CHECK(!card_format_run(&format));
    CHECK(!fat_mount(&volume, format.device));
    CHECK(image.written < 16 + CARD_FORMAT_BUFFER_SECTORS);
    image.cancel_after = 0;
    image.last_progress = 0;
    CHECK(card_format_run(&format));
    CHECK(fat_mount(&volume, format.device));
    fclose(image.file);
    CHECK_EQUAL(0, unlink(ECHO_FORMAT_IMAGE));
}

static void a_quick_format_preserves_data_outside_the_system_areas(void)
{
    Image image;
    CHECK(open_image(&image, ECHO_FORMAT_IMAGE, 256 * 2 * 1024 * 1024));
    uint8_t sentinel[512];
    memset(sentinel, 0xA5, sizeof sentinel);
    CHECK(write_image(&image, image.sectors - 1, 1, sentinel));
    CHECK(card_format_plan(&format.plan, image.sectors, 0));
    CHECK(card_format_run(&format));
    memset(sentinel, 0, sizeof sentinel);
    CHECK(read_image(&image, image.sectors - 1, 1, sentinel));
    CHECK_EQUAL(0xA5, sentinel[511]);
    fclose(image.file);
    CHECK_EQUAL(0, unlink(ECHO_FORMAT_IMAGE));
}

static void an_inconsistent_plan_is_refused_before_any_write(void)
{
    Image image;
    CHECK(open_image(&image, ECHO_FORMAT_IMAGE, 64 * 2 * 1024 * 1024));
    CHECK(card_format_plan(&format.plan, image.sectors, 0));
    format.plan.root_cluster++;
    CHECK(!card_format_run(&format));
    CHECK_EQUAL(0, image.calls);
    fclose(image.file);
    CHECK_EQUAL(0, unlink(ECHO_FORMAT_IMAGE));
}

int main(int argc, char **argv)
{
    if (argc == 3) {
        return format_image(argv[2], (uint32_t)strtoul(argv[1], NULL, 10)) ? 0 : 1;
    }
    static const TestCase cases[] = {
        TEST_CASE(the_sd_capacity_boundary_chooses_fat32_then_exfat),
        TEST_CASE(unsupported_capacities_and_geometry_are_refused_before_writing),
        TEST_CASE(allocation_units_align_both_the_partition_and_the_data),
        TEST_CASE(interrupted_formats_never_publish_a_mountable_partition),
        TEST_CASE(cancellation_leaves_the_card_unmountable_and_a_retry_succeeds),
        TEST_CASE(a_quick_format_preserves_data_outside_the_system_areas),
        TEST_CASE(an_inconsistent_plan_is_refused_before_any_write),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
