#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "discovery_preferences.h"
#include "discovery_screen.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"

static FixtureLibrary fixture;
static PersonalTags tags;

static DiscoveryScreen fresh_screen(void)
{
    DiscoveryScreen screen = {.config.library = &fixture.library};
    screen.query = library_view_defaults();
    return screen;
}

static void start(void)
{
    fake_hal_reset();
    fixture_library_open(&fixture);
    tags = (PersonalTags){.load = STORE_READY, .result = STORE_DONE};
    tags.catalog.active = UINT32_C(1) << 2;
    memcpy(tags.catalog.names[2], "Workout", sizeof "Workout");
}

static DiscoveryScreen saved_screen(void)
{
    DiscoveryScreen screen = fresh_screen();
    memcpy(screen.query.text, "Bj\xC3\xB6rk", sizeof "Bj\xC3\xB6rk");
    screen.query.sort = LIBRARY_VIEW_SORT_YEAR;
    screen.query.descending = true;
    screen.query.artist = 0;
    screen.query.album = 1;
    screen.query.genre = 0;
    screen.query.year_min = 1980;
    screen.query.year_max = 2026;
    screen.query.duration_min_ms = 60000;
    screen.query.duration_max_ms = 600000;
    screen.required_tags = UINT32_C(1) << 2;
    CHECK(discovery_preferences_save(&screen, &tags));
    return screen;
}

static void a_search_and_combined_view_constraints_survive_a_restart(void)
{
    start();
    DiscoveryScreen saved = saved_screen();
    DiscoveryScreen restored = fresh_screen();
    CHECK(discovery_preferences_load(&restored, &tags));
    CHECK(strcmp(saved.query.text, restored.query.text) == 0);
    CHECK_EQUAL(LIBRARY_VIEW_SORT_YEAR, restored.query.sort);
    CHECK(restored.query.descending);
    CHECK_EQUAL(0, restored.query.artist);
    CHECK_EQUAL(1, restored.query.album);
    CHECK_EQUAL(1980, restored.query.year_min);
    CHECK_EQUAL(600000, restored.query.duration_max_ms);
    CHECK_EQUAL(UINT32_C(1) << 2, restored.required_tags);
}

static void changed_library_contents_cannot_reuse_numeric_metadata_ids(void)
{
    start();
    saved_screen();
    fixture.bytes[fixture.library.sections[LIBRARY_STRINGS].offset + 1] ^= 1;
    CHECK(fixture_library_identify(&fixture.library));
    DiscoveryScreen restored = fresh_screen();
    CHECK(discovery_preferences_load(&restored, &tags));
    CHECK_EQUAL(LIBRARY_NONE, restored.query.artist);
    CHECK_EQUAL(LIBRARY_NONE, restored.query.album);
    CHECK_EQUAL(LIBRARY_NONE, restored.query.genre);
    CHECK_EQUAL(1980, restored.query.year_min);
    CHECK_EQUAL(LIBRARY_VIEW_SORT_YEAR, restored.query.sort);
    CHECK(restored.query.text[0] != 0);
}

static void deleting_and_reusing_a_label_cannot_reuse_its_saved_filter(void)
{
    start();
    saved_screen();
    memset(tags.catalog.names[2], 0, PERSONAL_TAG_NAME_BYTES);
    memcpy(tags.catalog.names[2], "Sleep", sizeof "Sleep");
    DiscoveryScreen restored = fresh_screen();
    CHECK(discovery_preferences_load(&restored, &tags));
    CHECK_EQUAL(0, restored.required_tags);
}

static void deferred_tag_validation_keeps_edits_made_after_loading(void)
{
    start();
    saved_screen();
    DiscoveryScreen restored = fresh_screen();
    CHECK(discovery_preferences_load(&restored, NULL));
    memcpy(restored.query.text, "new search", sizeof "new search");
    discovery_preferences_validate_tags(&restored, &tags);
    CHECK(strcmp(restored.query.text, "new search") == 0);
    CHECK_EQUAL(UINT32_C(1) << 2, restored.required_tags);
}

static void damaged_preferences_are_rejected_without_changing_the_view(void)
{
    start();
    saved_screen();
    const char *path = "/" FIRMWARE_DATA_DIRECTORY "/discovery.bin";
    uint8_t bytes[112];
    CHECK(hal_internal_file_read(path, bytes, sizeof bytes));
    bytes[44] ^= 1;
    CHECK(hal_internal_file_write(path, bytes, sizeof bytes));
    DiscoveryScreen restored = fresh_screen();
    CHECK(!discovery_preferences_load(&restored, &tags));
    CHECK_EQUAL(0, restored.query.text[0]);
    CHECK_EQUAL(0, restored.required_tags);
}

static void every_header_and_query_boundary_is_validated(void)
{
    start();
    saved_screen();
    const char *path = "/" FIRMWARE_DATA_DIRECTORY "/discovery.bin";
    uint8_t valid[112];
    CHECK(hal_internal_file_read(path, valid, sizeof valid));
    static const struct {
        uint8_t offset;
        uint8_t value;
        bool reseal;
    } header_cases[] = {
        {0, 'X', true}, {1, 'X', true},       {2, 'X', true}, {3, 'X', true},
        {4, 2, true},   {5, UINT8_MAX, true}, {6, 2, true},   {108, 1, false},
    };
    for (uint32_t index = 0; index < sizeof header_cases / sizeof header_cases[0]; index++) {
        uint8_t bytes[sizeof valid];
        memcpy(bytes, valid, sizeof bytes);
        bytes[header_cases[index].offset] = header_cases[index].value;
        if (header_cases[index].reseal) {
            store_put32(bytes + 108, hash_update(HASH_START, bytes, 108));
        }
        CHECK(hal_internal_file_write(path, bytes, sizeof bytes));
        DiscoveryScreen restored = fresh_screen();
        CHECK(!discovery_preferences_load(&restored, &tags));
    }

    uint8_t bytes[sizeof valid];
    memcpy(bytes, valid, sizeof bytes);
    memset(bytes + 44, 'x', 64);
    store_put32(bytes + 108, hash_update(HASH_START, bytes, 108));
    CHECK(hal_internal_file_write(path, bytes, sizeof bytes));
    DiscoveryScreen restored = fresh_screen();
    CHECK(!discovery_preferences_load(&restored, &tags));

    static const struct {
        uint8_t offset;
        uint32_t value;
        bool wide;
    } query_cases[] = {
        {12, 0, true},
        {12, UINT32_MAX, true},
        {22, 2027, false},
        {28, 600001, true},
    };
    for (uint32_t index = 0; index < sizeof query_cases / sizeof query_cases[0]; index++) {
        memcpy(bytes, valid, sizeof bytes);
        if (query_cases[index].wide) {
            store_put32(bytes + query_cases[index].offset, query_cases[index].value);
        } else {
            store_put16(bytes + query_cases[index].offset, (uint16_t)query_cases[index].value);
        }
        store_put32(bytes + 108, hash_update(HASH_START, bytes, 108));
        CHECK(hal_internal_file_write(path, bytes, sizeof bytes));
        restored = fresh_screen();
        CHECK(!discovery_preferences_load(&restored, &tags));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_search_and_combined_view_constraints_survive_a_restart),
        TEST_CASE(changed_library_contents_cannot_reuse_numeric_metadata_ids),
        TEST_CASE(deleting_and_reusing_a_label_cannot_reuse_its_saved_filter),
        TEST_CASE(deferred_tag_validation_keeps_edits_made_after_loading),
        TEST_CASE(damaged_preferences_are_rejected_without_changing_the_view),
        TEST_CASE(every_header_and_query_boundary_is_validated),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
