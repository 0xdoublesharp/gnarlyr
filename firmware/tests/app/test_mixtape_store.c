#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "mixtape_store.h"

static MixtapeStore store;
static const MixIdentity SONGS[] = {{.path = "Music/One.flac"}, {.path = "Music/Two.flac"}};

static bool read_song(const void *context, uint16_t index, MixIdentity *identity)
{
    const MixIdentity *songs = context;
    *identity = songs[index];
    return true;
}

static MixtapeChange save(const char *name, uint16_t count)
{
    return (MixtapeChange){
        .operation = MIXTAPE_SAVE, .slot = 0, .name = name, .source = {read_song, SONGS, count}};
}

static void finish(void)
{
    for (int step = 0; step < 1000 && store.result == MIX_WORKING; step++) {
        mixtape_store_step(&store);
    }
}

static void reset(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    CHECK_EQUAL(MIX_EMPTY, mixtape_store_init(&store));
}

static void a_saved_mixtape_reopens_with_its_complete_sequence(void)
{
    reset();
    CHECK(mixtape_store_change(&store, save("Night drive", 2)));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(entry.active);
    CHECK(strcmp(entry.name, "Night drive") == 0);
    CHECK_EQUAL(2, entry.count);
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &store.tape);
    for (int index = 0; index < 2; index++) {
        MixIdentity identity;
        CHECK_EQUAL(MIX_READY, mix_tape_next(&cursor, &identity));
        CHECK(mix_identity_equal(&SONGS[index], &identity));
    }
}

static void cancellation_during_replacement_keeps_the_published_mix(void)
{
    for (int cutoff = 0; cutoff < 34; cutoff++) {
        a_saved_mixtape_reopens_with_its_complete_sequence();
        CHECK(mixtape_store_change(&store, save("Replacement", 1)));
        for (int step = 0; step < cutoff && store.result == MIX_WORKING; step++) {
            mixtape_store_step(&store);
        }
        bool published = store.result == MIX_DONE;
        mixtape_store_cancel(&store);
        CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
        CHECK(mixtape_store_open(&store, 0));
        finish();
        CHECK_EQUAL(MIX_DONE, store.result);
        CHECK_EQUAL(published ? 1 : 2, store.tape.count);
    }
}

static void a_failed_data_write_preserves_the_previous_name_and_songs(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    fake_hal_fail_file_writes("/.echo/MX00B.BIN");
    CHECK(mixtape_store_change(&store, save("Replacement", 1)));
    finish();
    CHECK_EQUAL(MIX_FAILED, store.result);
    fake_hal_fail_file_writes(NULL);
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK_EQUAL(2, store.tape.count);
    CHECK(strcmp(store.entry.name, "Night drive") == 0);
}

static void rename_delete_and_reuse_do_not_resurrect_an_old_sequence(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    CHECK(mixtape_store_change(
        &store, (MixtapeChange){.operation = MIXTAPE_RENAME, .slot = 0, .name = "Björk"}));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(strcmp(entry.name, "Björk") == 0);
    CHECK_EQUAL(2, entry.count);
    CHECK(mixtape_store_change(&store, (MixtapeChange){.operation = MIXTAPE_DELETE, .slot = 0}));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    CHECK(!mixtape_store_open(&store, 0));
    CHECK(mixtape_store_change(&store, save("Empty", 0)));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK_EQUAL(0, store.tape.count);
}

static void corrupt_new_data_recovers_one_entry_without_rolling_back_other_names(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    MixtapeChange other = save("Other", 1);
    other.slot = 1;
    CHECK(mixtape_store_change(&store, other));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK(mixtape_store_change(&store, save("Replacement", 1)));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    uint8_t bad = 0xff;
    CHECK(hal_file_write_at("/.echo/MX00B.BIN", MIX_HEADER_BYTES, &bad, 1));
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(MIX_DONE, store.result);
    CHECK(store.recovered);
    CHECK_EQUAL(2, store.tape.count);
    CHECK_EQUAL(MIX_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(strcmp(entry.name, "Night drive") == 0);
    CHECK(mixtape_store_read(&store, 1, &entry));
    CHECK(strcmp(entry.name, "Other") == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_saved_mixtape_reopens_with_its_complete_sequence),
        TEST_CASE(cancellation_during_replacement_keeps_the_published_mix),
        TEST_CASE(a_failed_data_write_preserves_the_previous_name_and_songs),
        TEST_CASE(rename_delete_and_reuse_do_not_resurrect_an_old_sequence),
        TEST_CASE(corrupt_new_data_recovers_one_entry_without_rolling_back_other_names),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
