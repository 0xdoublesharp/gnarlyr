#include "check.h"
#include "cover_cache.h"
#include "fixture_library.h"
#include "hal/system.h"
#include "library_store.h"
#include "sim_storage.h"

static LibraryStore store;
static CoverCache covers;
static int beats;

void hal_system_keep_alive(void)
{
    beats++;
}

static void prepare(void)
{
    sim_storage_close_all();
    sim_storage_configure(NULL, ECHO_TEST_OUTPUT_DIR);
    store = (LibraryStore){0};
    covers = (CoverCache){0};
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    CHECK(hal_file_write(LIBRARY_STORE_PATH, bytes, size));
    bytes = fixture_covers_bytes(&size);
    CHECK(hal_file_write(COVER_CACHE_PATH, bytes, size));
    beats = 0;
}

static void repeated_database_reload_keeps_handles_available_for_playback(void)
{
    prepare();
    for (int reload = 0; reload < 24; reload++) {
        CHECK(library_store_open(&store));
        cover_cache_open(&covers, &store.library);
        CHECK(covers.available);
    }
    CHECK(beats > 0);
    HalFile files[6];
    for (int index = 0; index < 6; index++) {
        CHECK(hal_file_open(&files[index], LIBRARY_STORE_PATH));
    }
    for (int index = 0; index < 6; index++) {
        hal_file_close(&files[index]);
    }
    sim_storage_close_all();
}

static void a_failed_reload_closes_its_file_and_discards_the_old_identity(void)
{
    prepare();
    static const uint8_t BAD[8] = {0};
    for (int reload = 0; reload < 12; reload++) {
        CHECK(library_store_open(&store));
        CHECK(hal_file_write(LIBRARY_STORE_PATH, BAD, sizeof BAD));
        CHECK(!library_store_open(&store));
        CHECK(!store.available);
        CHECK_EQUAL(0, library_fingerprint(&store.library));
        uint32_t size = 0;
        const uint8_t *bytes = fixture_bytes(&size);
        CHECK(hal_file_write(LIBRARY_STORE_PATH, bytes, size));
    }
    HalFile files[8];
    for (int index = 0; index < 8; index++) {
        CHECK(hal_file_open(&files[index], LIBRARY_STORE_PATH));
    }
    for (int index = 0; index < 8; index++) {
        hal_file_close(&files[index]);
    }
    sim_storage_close_all();
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(repeated_database_reload_keeps_handles_available_for_playback),
        TEST_CASE(a_failed_reload_closes_its_file_and_discards_the_old_identity),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
