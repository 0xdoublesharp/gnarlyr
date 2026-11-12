#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "mixtape_store.h"

static MixtapeStore store;
static MixIdentity SONGS[] = {{.path = "Music/One.flac"}, {.path = "Music/Two.flac"}};

static bool read_song(void *context, uint16_t index, MixIdentity *identity)
{
    const MixIdentity *songs = context;
    *identity = songs[index];
    return true;
}

typedef struct FailingSource {
    uint32_t reads;
    uint32_t fail_at_read;
} FailingSource;

static bool read_failing_source(void *context, uint16_t index, MixIdentity *identity)
{
    FailingSource *source = context;
    if (source->reads++ == source->fail_at_read) {
        return false;
    }
    *identity = SONGS[index % 2];
    return true;
}

static MixtapeChange save(const char *name, uint16_t count)
{
    return (MixtapeChange){
        .operation = MIXTAPE_SAVE, .slot = 0, .name = name, .source = {read_song, SONGS, count}};
}

static void finish(void)
{
    for (int step = 0; step < 1000 && store.result == STORE_WORKING; step++) {
        mixtape_store_step(&store);
    }
}

static void reset(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    CHECK_EQUAL(STORE_EMPTY, mixtape_store_init(&store));
}

static void a_saved_mixtape_reopens_with_its_complete_sequence(void)
{
    reset();
    CHECK(mixtape_store_change(&store, save("Night drive", 2)));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(entry.active);
    CHECK(strcmp(entry.name, "Night drive") == 0);
    CHECK_EQUAL(2, entry.count);
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &store.tape);
    for (int index = 0; index < 2; index++) {
        MixIdentity identity;
        CHECK_EQUAL(STORE_READY, mix_tape_next(&cursor, &identity));
        CHECK(mix_identity_equal(&SONGS[index], &identity));
    }
}

static void cancellation_during_replacement_keeps_the_published_mix(void)
{
    for (int cutoff = 0; cutoff < 34; cutoff++) {
        a_saved_mixtape_reopens_with_its_complete_sequence();
        CHECK(mixtape_store_change(&store, save("Replacement", 1)));
        for (int step = 0; step < cutoff && store.result == STORE_WORKING; step++) {
            mixtape_store_step(&store);
        }
        bool published = store.result == STORE_DONE;
        mixtape_store_cancel(&store);
        CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
        CHECK(mixtape_store_open(&store, 0));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        CHECK_EQUAL(published ? 1 : 2, store.tape.count);
    }
}

static void a_failed_data_write_preserves_the_previous_name_and_songs(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    fake_hal_fail_file_writes("/" FIRMWARE_DATA_DIRECTORY "/mix00b.bin");
    CHECK(mixtape_store_change(&store, save("Replacement", 1)));
    finish();
    CHECK_EQUAL(STORE_FAILED, store.result);
    fake_hal_fail_file_writes(NULL);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(2, store.tape.count);
    CHECK(strcmp(store.entry.name, "Night drive") == 0);
}

static void rename_delete_and_reuse_do_not_resurrect_an_old_sequence(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    CHECK(mixtape_store_change(
        &store, (MixtapeChange){.operation = MIXTAPE_RENAME, .slot = 0, .name = "Björk"}));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(strcmp(entry.name, "Björk") == 0);
    CHECK_EQUAL(2, entry.count);
    CHECK(mixtape_store_change(&store, (MixtapeChange){.operation = MIXTAPE_DELETE, .slot = 0}));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(!mixtape_store_open(&store, 0));
    CHECK(mixtape_store_change(&store, save("Empty", 0)));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(0, store.tape.count);
}

static void corrupt_new_data_recovers_one_entry_without_rolling_back_other_names(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    MixtapeChange other = save("Other", 1);
    other.slot = 1;
    CHECK(mixtape_store_change(&store, other));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK(mixtape_store_change(&store, save("Replacement", 1)));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    uint8_t bad = 0xff;
    CHECK(hal_file_write_at("/" FIRMWARE_DATA_DIRECTORY "/mix00b.bin", STORE_BLOCK_BYTES, &bad, 1));
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK(store.recovered);
    CHECK_EQUAL(2, store.tape.count);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(strcmp(entry.name, "Night drive") == 0);
    CHECK(mixtape_store_read(&store, 1, &entry));
    CHECK(strcmp(entry.name, "Other") == 0);
}

static void invalid_operations_and_names_do_not_change_a_published_mix(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    int reserves = fake_hal_reserve_count();
    CHECK(!mixtape_store_change(
        &store, (MixtapeChange){.operation = (MixtapeOperation)-1, .slot = 0, .name = "Intruder"}));
    CHECK(!mixtape_store_change(
        &store, (MixtapeChange){.operation = MIXTAPE_DELETE + 1, .slot = 0, .name = "Intruder"}));
    CHECK(!mixtape_store_change(
        &store,
        (MixtapeChange){.operation = MIXTAPE_SAVE, .slot = MIX_CATALOG_SLOTS, .name = "Intruder"}));
    CHECK(!mixtape_store_change(
        &store, (MixtapeChange){.operation = MIXTAPE_RENAME, .slot = 1, .name = "Unused"}));
    CHECK(!mixtape_store_change(&store, (MixtapeChange){.operation = MIXTAPE_DELETE, .slot = 1}));
    static const char too_long[MIX_NAME_BYTES + 1] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                                                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    const char *invalid_names[] = {NULL, "", "    ", "Bad\n", too_long};
    for (uint32_t index = 0; index < sizeof invalid_names / sizeof invalid_names[0]; index++) {
        CHECK(!mixtape_store_change(
            &store,
            (MixtapeChange){.operation = MIXTAPE_RENAME, .slot = 0, .name = invalid_names[index]}));
    }
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(reserves, fake_hal_reserve_count());
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(strcmp(entry.name, "Night drive") == 0);
}

static void catalog_allocation_failures_can_be_retried(void)
{
    static const HalReserveResult failures[] = {HAL_RESERVE_FAILED, HAL_RESERVE_EXISTS};
    for (uint32_t index = 0; index < sizeof failures / sizeof failures[0]; index++) {
        reset();
        fake_hal_set_reserve_result(failures[index]);
        CHECK(!mixtape_store_change(&store, save("Retry", 1)));
        CHECK_EQUAL(STORE_IDLE, store.result);
        fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
        CHECK(mixtape_store_change(&store, save("Retry", 1)));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
        CHECK(mixtape_store_open(&store, 0));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        CHECK_EQUAL(1, store.tape.count);
        CHECK(strcmp(store.entry.name, "Retry") == 0);
    }
    reset();
    fake_hal_fail_file_opens(MIX_CATALOG_PATH);
    CHECK(!mixtape_store_change(&store, save("Retry", 1)));
    CHECK_EQUAL(STORE_IDLE, store.result);
    fake_hal_fail_file_opens(NULL);
    CHECK(mixtape_store_change(&store, save("Retry", 1)));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(1, store.tape.count);
    CHECK(strcmp(store.entry.name, "Retry") == 0);
}

static void a_late_source_failure_never_publishes_a_partial_mix(void)
{
    reset();
    FailingSource source = {.fail_at_read = 34 + 32};
    MixtapeChange change = save("Long mix", 34);
    change.source = (MixSource){read_failing_source, &source, 34};
    CHECK(mixtape_store_change(&store, change));
    finish();
    CHECK_EQUAL(STORE_FAILED, store.result);
    CHECK(source.reads > change.source.count);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    MixEntry entry;
    CHECK(mixtape_store_read(&store, 0, &entry));
    CHECK(!entry.active);
    source = (FailingSource){.fail_at_read = UINT32_MAX};
    CHECK(mixtape_store_change(&store, change));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(34, store.tape.count);
}

static void replacement_allocation_and_write_failures_preserve_the_published_mix(void)
{
    for (uint32_t failure = 0; failure < 2; failure++) {
        a_saved_mixtape_reopens_with_its_complete_sequence();
        char path[MIX_TAPE_PATH_BYTES];
        CHECK(mix_tape_path(0, 1, path));
        fake_hal_add_file(path, NULL, 1);
        if (failure == 0) {
            fake_hal_fail_file_writes(path);
        } else {
            fake_hal_set_reserve_result(HAL_RESERVE_FAILED);
        }
        CHECK(mixtape_store_change(&store, save("Replacement", 1)));
        finish();
        CHECK_EQUAL(STORE_FAILED, store.result);
        fake_hal_fail_file_writes(NULL);
        fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
        CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
        CHECK(mixtape_store_open(&store, 0));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        CHECK_EQUAL(2, store.tape.count);
        CHECK(strcmp(store.entry.name, "Night drive") == 0);
    }
}

static void a_failed_catalog_checkpoint_preserves_the_published_mix(void)
{
    a_saved_mixtape_reopens_with_its_complete_sequence();
    fake_hal_fail_file_writes(MIX_CATALOG_PATH);
    CHECK(mixtape_store_change(&store, save("Replacement", 1)));
    finish();
    CHECK_EQUAL(STORE_FAILED, store.result);
    fake_hal_fail_file_writes(NULL);
    CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
    CHECK(mixtape_store_open(&store, 0));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);
    CHECK_EQUAL(2, store.tape.count);
    CHECK(strcmp(store.entry.name, "Night drive") == 0);
}

static void a_missing_or_unreadable_current_bank_recovers_the_previous_mix(void)
{
    for (uint32_t failure = 0; failure < 2; failure++) {
        a_saved_mixtape_reopens_with_its_complete_sequence();
        CHECK(mixtape_store_change(&store, save("Replacement", 1)));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        char path[MIX_TAPE_PATH_BYTES];
        CHECK(mix_tape_path(0, 1, path));
        if (failure == 0) {
            CHECK(hal_file_remove(path));
        } else {
            fake_hal_fail_file_reads(path);
        }
        CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
        CHECK(mixtape_store_open(&store, 0));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        CHECK(store.recovered);
        CHECK_EQUAL(2, store.tape.count);
        CHECK(strcmp(store.entry.name, "Night drive") == 0);
        fake_hal_fail_file_reads(NULL);
        CHECK_EQUAL(STORE_READY, mixtape_store_init(&store));
        CHECK(mixtape_store_open(&store, 0));
        finish();
        CHECK_EQUAL(STORE_DONE, store.result);
        CHECK(!store.recovered);
    }
}

static void cassette_side_limits_and_public_state_guards_fail_closed(void)
{
    reset();
    MixtapeChange change = save("Split", 2);
    change.side_minutes = 1;
    CHECK(!mixtape_store_change(&store, change));
    change.side_minutes = 30;
    change.side_a_count = 3;
    CHECK(!mixtape_store_change(&store, change));
    change.source.count = 65;
    change.side_a_count = 65;
    CHECK(!mixtape_store_change(&store, change));
    change.source.count = 130;
    change.side_a_count = 64;
    CHECK(!mixtape_store_change(&store, change));
    change = save("Split", 2);
    change.side_minutes = 30;
    change.side_a_count = 1;
    CHECK(mixtape_store_change(&store, change));
    finish();
    CHECK_EQUAL(STORE_DONE, store.result);

    MixEntry entry = {.active = true};
    MixtapeStore state = {.load = STORE_UNREADABLE};
    CHECK(!mixtape_store_read(&state, 0, &entry));
    CHECK(entry.active);
    state.load = STORE_EMPTY;
    CHECK(!mixtape_store_read(&state, MIX_CATALOG_SLOTS, &entry));
    CHECK(mixtape_store_read(&state, 0, &entry));
    CHECK(!entry.active);

    state.result = STORE_WORKING;
    CHECK(!mixtape_store_change(&state, save("Busy", 0)));
    CHECK(!mixtape_store_open(&state, 0));
    state.result = STORE_IDLE;
    CHECK_EQUAL(STORE_IDLE, mixtape_store_step(&state));
    mixtape_store_cancel(&state);
    CHECK_EQUAL(STORE_IDLE, state.result);

    state = (MixtapeStore){.result = STORE_WORKING, .stage = UINT8_MAX};
    CHECK_EQUAL(STORE_FAILED, mixtape_store_step(&state));

    static const uint8_t CANCEL_STAGES[] = {1, 2, 4, 5, 7};
    for (uint32_t index = 0; index < sizeof CANCEL_STAGES; index++) {
        state = (MixtapeStore){.result = STORE_WORKING, .stage = CANCEL_STAGES[index]};
        mixtape_store_cancel(&state);
        CHECK_EQUAL(STORE_FAILED, state.result);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_saved_mixtape_reopens_with_its_complete_sequence),
        TEST_CASE(cancellation_during_replacement_keeps_the_published_mix),
        TEST_CASE(a_failed_data_write_preserves_the_previous_name_and_songs),
        TEST_CASE(rename_delete_and_reuse_do_not_resurrect_an_old_sequence),
        TEST_CASE(corrupt_new_data_recovers_one_entry_without_rolling_back_other_names),
        TEST_CASE(invalid_operations_and_names_do_not_change_a_published_mix),
        TEST_CASE(catalog_allocation_failures_can_be_retried),
        TEST_CASE(a_late_source_failure_never_publishes_a_partial_mix),
        TEST_CASE(replacement_allocation_and_write_failures_preserve_the_published_mix),
        TEST_CASE(a_failed_catalog_checkpoint_preserves_the_published_mix),
        TEST_CASE(a_missing_or_unreadable_current_bank_recovers_the_previous_mix),
        TEST_CASE(cassette_side_limits_and_public_state_guards_fail_closed),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
