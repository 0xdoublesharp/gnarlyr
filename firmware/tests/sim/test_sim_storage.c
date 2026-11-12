#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "hal/storage.h"
#include "playback/listening_export.h"
#include "sim_storage.h"

typedef struct Found {
    bool found;
    HalDirEntry entry;
} Found;

static Found find(const char *path, const char *name)
{
    Found result = {.found = false};
    HalDir dir;
    CHECK(hal_dir_open(&dir, path));
    static HalDirEntry entry;
    while (hal_dir_next(&dir, &entry)) {
        if (strcmp(entry.name, name) == 0) {
            result = (Found){.found = true, .entry = entry};
        }
    }
    hal_dir_close(&dir);
    return result;
}

static void the_card_root_lists_its_folders(void)
{
    sim_storage_configure(ECHO_FIXTURE_DIR, NULL);
    Found flac = find("/", "flac");
    CHECK(flac.found);
    CHECK(flac.entry.directory);
}

static void a_folder_lists_its_files_with_sizes(void)
{
    sim_storage_configure(ECHO_FIXTURE_DIR, NULL);
    Found file = find("/flac", "big_blocks_16.flac");
    CHECK(file.found);
    CHECK(!file.entry.directory);
    CHECK(file.entry.size > 0);
    CHECK(!find("/flac", ".").found);
    CHECK(!find("/flac", "..").found);
}

static void a_missing_folder_does_not_open(void)
{
    sim_storage_configure(ECHO_FIXTURE_DIR, NULL);
    HalDir dir;
    CHECK(!hal_dir_open(&dir, "/no such folder"));
    CHECK(!hal_dir_open(&dir, "/small_library.db"));
}

static void a_read_after_an_overwrite_sees_the_new_bytes(void)
{
    static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/overwritten.bin";
    static const uint8_t FRESH[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static uint8_t zeros[8192];
    uint8_t seen[sizeof FRESH];
    HalFile file;
    sim_storage_configure(ECHO_FIXTURE_DIR, ECHO_TEST_OUTPUT_DIR);
    CHECK(hal_file_write(PATH, zeros, sizeof zeros));
    CHECK(hal_file_open(&file, PATH));
    CHECK(hal_file_read(file, 4096, seen, sizeof seen));
    CHECK(hal_file_write_at(PATH, 4100, FRESH, sizeof FRESH));
    CHECK(hal_file_read(file, 4100, seen, sizeof seen));
    hal_file_close(&file);
    CHECK(memcmp(seen, FRESH, sizeof FRESH) == 0);
}

static void deleting_a_system_file_keeps_the_music_files(void)
{
    static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/removed.bin";
    static const uint8_t BYTES[] = {1, 2, 3, 4};
    HalFile file;
    sim_storage_configure(ECHO_FIXTURE_DIR, ECHO_TEST_OUTPUT_DIR);
    CHECK(hal_file_write(PATH, BYTES, sizeof BYTES));
    CHECK(hal_file_remove(PATH));
    CHECK(!hal_file_open(&file, PATH));
    CHECK(!hal_file_remove("/small_library.db"));
    CHECK(hal_file_open(&file, "/small_library.db"));
    hal_file_close(&file);
}

static void the_play_export_at_the_card_root_can_be_replaced(void)
{
    HalFile file;
    sim_storage_configure(ECHO_TEST_OUTPUT_DIR, NULL);
    (void)hal_file_remove(LISTENING_EXPORT_PATH);
    CHECK_EQUAL(HAL_RESERVE_CREATED, hal_file_reserve(LISTENING_EXPORT_PATH, 100));
    CHECK(hal_file_remove(LISTENING_EXPORT_PATH));
    CHECK(!hal_file_open(&file, LISTENING_EXPORT_PATH));
    CHECK_EQUAL(HAL_RESERVE_CREATED, hal_file_reserve(LISTENING_EXPORT_PATH, 40));
    CHECK(hal_file_open(&file, LISTENING_EXPORT_PATH));
    CHECK_EQUAL(40, file.size);
    hal_file_close(&file);
    CHECK(hal_file_remove(LISTENING_EXPORT_PATH));
}

static void theme_packs_follow_the_music_volume_while_selection_stays_internal(void)
{
    char path[1024];
    static const char theme[] = "/" FIRMWARE_DATA_DIRECTORY "/themes/original.gtheme";
    static const char selection[] = "/" FIRMWARE_DATA_DIRECTORY "/theme.bin";
    sim_storage_configure("card", "internal");
    CHECK(sim_storage_host_path(theme, path, sizeof path));
    CHECK(strcmp(path, "card"
                       "/" FIRMWARE_DATA_DIRECTORY "/themes/original.gtheme") == 0);
    CHECK(sim_storage_host_path(selection, path, sizeof path));
    CHECK(strcmp(path, "internal/theme.bin") == 0);
    sim_storage_configure(NULL, "internal");
    CHECK(sim_storage_host_path(theme, path, sizeof path));
    CHECK(strcmp(path, "internal/themes/original.gtheme") == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_card_root_lists_its_folders),
        TEST_CASE(a_folder_lists_its_files_with_sizes),
        TEST_CASE(a_missing_folder_does_not_open),
        TEST_CASE(a_read_after_an_overwrite_sees_the_new_bytes),
        TEST_CASE(deleting_a_system_file_keeps_the_music_files),
        TEST_CASE(the_play_export_at_the_card_root_can_be_replaced),
        TEST_CASE(theme_packs_follow_the_music_volume_while_selection_stays_internal),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
