#include <string.h>

#include "check.h"
#include "core/bytes.h"
#include "core/hash.h"
#include "playback/session_journal.h"

enum {
    FINGERPRINT = 0x12345678,
    TRACK_LIMIT = 10,
    VERSION_FIELD = 4,
    GENERATION_FIELD = 8,
    BINDING_FIELD = 20,
    CHECK_FIELD = 24,
    RECORD_BYTES_USED = STORE_FIELD_BYTES + RESUME_STATE_SIZE + UP_NEXT_STATE_SIZE + 8
};

static uint8_t disk[SESSION_FILE_BYTES];
static SessionJournal journal;
static uint32_t write_bytes = SESSION_RECORD_BYTES;
static bool write_result = true;
static bool refuse_reads;
static bool refuse_after_write;

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (refuse_reads || offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    if (offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    uint32_t amount = length < write_bytes ? length : write_bytes;
    memcpy(disk + offset, bytes, amount);
    refuse_reads = refuse_after_write;
    return write_result;
}

static SessionJournalConfig configuration(void)
{
    return (SessionJournalConfig){
        .file = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk},
        .fingerprint = FINGERPRINT,
        .track_limit = TRACK_LIMIT};
}

static SessionState sample(void)
{
    SessionState state = {.resume = {.library_fingerprint = FINGERPRINT,
                                     .plan = {.source = {.kind = BROWSE_ALBUM_TRACKS,
                                                         .group = LIBRARY_ARTISTS,
                                                         .subject = 0,
                                                         .artist = LIBRARY_NONE},
                                              .anchor_track = 0,
                                              .shuffle = SHUFFLE_OFF,
                                              .seed = 7},
                                     .track = 0,
                                     .position_ms = 12345,
                                     .volume = 12},
                          .queue_kind = SESSION_QUEUE_GENERATED};
    up_next_add(&state.up_next, 2);
    return state;
}

static void a_session_and_its_pending_songs_are_saved_together(void)
{
    memset(disk, 0, sizeof disk);
    SessionState restored = {0};
    CHECK_EQUAL(STORE_EMPTY, session_journal_open(&journal, configuration(), &restored));
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
    CHECK_EQUAL(1, restored.up_next.count);
    CHECK_EQUAL(2, restored.up_next.tracks[0]);
}

static void reset(void)
{
    memset(disk, 0, sizeof disk);
    write_bytes = SESSION_RECORD_BYTES;
    write_result = true;
    refuse_reads = false;
    refuse_after_write = false;
    SessionState restored;
    CHECK_EQUAL(STORE_EMPTY, session_journal_open(&journal, configuration(), &restored));
}

static void rehash(uint8_t *bytes)
{
    bytes_put_le32(bytes + CHECK_FIELD, hash_update(HASH_START, bytes, CHECK_FIELD));
}

static void torn_writes_preserve_a_complete_session(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    uint8_t before[SESSION_FILE_BYTES];
    memcpy(before, disk, sizeof before);
    for (uint32_t cut = 0; cut <= SESSION_RECORD_BYTES; cut++) {
        memcpy(disk, before, sizeof before);
        SessionState restored;
        CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
        state.resume.position_ms = 98765;
        up_next_clear(&state.up_next);
        write_bytes = cut;
        write_result = false;
        StoreResult result = session_journal_save(&journal, &state);
        CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
        CHECK_EQUAL(result == STORE_DONE ? 98765 : 12345, restored.resume.position_ms);
        CHECK_EQUAL(result == STORE_DONE ? 0 : 1, restored.up_next.count);
        CHECK(memcmp(before, disk, SESSION_RECORD_BYTES) == 0);
    }
}

static void failed_readback_requires_reload_before_another_write(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    state.resume.position_ms = 4444;
    refuse_after_write = true;
    CHECK_EQUAL(STORE_UNCERTAIN, session_journal_save(&journal, &state));
    CHECK(journal.needs_reload);
    refuse_reads = false;
    refuse_after_write = false;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    SessionState restored;
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(4444, restored.resume.position_ms);
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
}

static void previous_session_is_available_and_corrupt_newest_is_ignored(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    state.resume.position_ms = 5555;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    SessionState restored;
    CHECK_EQUAL(STORE_READY, session_journal_previous(&journal, &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
    disk[SESSION_RECORD_BYTES + 48] ^= 1;
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
    disk[48] ^= 1;
    CHECK_EQUAL(STORE_CORRUPT, session_journal_open(&journal, configuration(), &restored));
}

static void future_versions_are_preserved_and_invalid_ids_never_write(void)
{
    reset();
    SessionState state = sample();
    state.up_next.count = UP_NEXT_CAPACITY + 1;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = sample();
    state.up_next.tracks[0] = TRACK_LIMIT;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    disk[VERSION_FIELD] = 2;
    rehash(disk);
    SessionState restored;
    CHECK_EQUAL(STORE_UNSUPPORTED, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    CHECK_EQUAL(2, disk[VERSION_FIELD]);
}

static void stored_generation_zero_and_serial_wrap_are_supported(void)
{
    reset();
    SessionState state = sample();
    state.queue_kind = SESSION_QUEUE_STORED;
    up_next_clear(&state.up_next);
    state.order_generation = 0;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    bytes_put_le32(disk + GENERATION_FIELD, UINT32_MAX);
    rehash(disk);
    SessionState restored;
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(UINT32_MAX, journal.generation);
    state.resume.position_ms = 6666;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    CHECK_EQUAL(0, journal.generation);
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(6666, restored.resume.position_ms);
    CHECK_EQUAL(0, restored.order_generation);
    CHECK_EQUAL(SESSION_QUEUE_STORED, restored.queue_kind);
}

static void a_session_of_another_library_reads_as_empty_and_is_replaced(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    SessionJournalConfig other = configuration();
    other.fingerprint++;
    SessionState restored = {.order_generation = 77};
    CHECK_EQUAL(STORE_EMPTY, session_journal_open(&journal, other, &restored));
    CHECK_EQUAL(77, restored.order_generation);
    CHECK(!journal.needs_reload);
    state.resume.library_fingerprint = other.fingerprint;
    state.resume.position_ms = 2222;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, other, &restored));
    CHECK_EQUAL(2222, restored.resume.position_ms);
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
}

static void a_refused_write_is_a_definite_failure_that_keeps_both_records(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    state.resume.position_ms = 5555;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    state.resume.position_ms = 7777;
    write_bytes = 0;
    write_result = false;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    CHECK(!journal.needs_reload);
    write_bytes = SESSION_RECORD_BYTES;
    write_result = true;
    SessionState restored;
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(5555, restored.resume.position_ms);
    CHECK_EQUAL(STORE_READY, session_journal_previous(&journal, &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
}

static void a_record_is_one_block_with_its_payload_inside(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    CHECK(memcmp(disk, "DSSJ", 4) == 0);
    CHECK_EQUAL(1, disk[VERSION_FIELD]);
    CHECK_EQUAL(0x78, disk[BINDING_FIELD]);
    for (int index = RECORD_BYTES_USED; index < SESSION_FILE_BYTES; index++) {
        CHECK_EQUAL(0, disk[index]);
    }
    CHECK(memcmp(SESSION_JOURNAL_PATH + strlen(SESSION_JOURNAL_PATH) - 12, "/session.bin", 12) ==
          0);
}

static void invalid_configurations_and_queue_states_fail_closed(void)
{
    SessionState restored = {0};
    SessionJournalConfig config = configuration();

    config.file.source.read = NULL;
    CHECK_EQUAL(STORE_UNREADABLE, session_journal_open(&journal, config, &restored));
    config = configuration();
    config.file.size = SESSION_FILE_BYTES - 1;
    CHECK_EQUAL(STORE_UNREADABLE, session_journal_open(&journal, config, &restored));
    config = configuration();
    config.track_limit = (uint32_t)UINT16_MAX + 1u;
    CHECK_EQUAL(STORE_UNREADABLE, session_journal_open(&journal, config, &restored));

    reset();
    SessionState state = sample();
    journal.config.file.write = NULL;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    journal.config = configuration();

    state.queue_kind = (SessionQueueKind)UINT8_MAX;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = sample();
    state.order_generation = 1;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = sample();
    state.queue_kind = SESSION_QUEUE_STORED;
    state.resume.from_up_next = true;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.resume.from_up_next = false;
    state.resume.cursor.album_track = 1;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.resume.cursor.album_track = 0;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    up_next_clear(&state.up_next);
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));

    state = sample();
    state.queue_kind = SESSION_QUEUE_EMPTY;
    state.resume.from_up_next = false;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.resume.from_up_next = true;
    state.order_generation = 1;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
}

static SessionState cassette_session(void)
{
    SessionState state = sample();
    up_next_clear(&state.up_next);
    state.queue_kind = SESSION_QUEUE_STORED;
    state.cassette = (CassetteState){
        .generation = 7, .count = 8, .split = 4, .slot = 2, .side = 0, .active = true};
    state.resume.cursor.position = 0;
    return state;
}

static void invalid_cassette_states_are_never_persisted(void)
{
    reset();
    SessionState state = sample();
    state.cassette.generation = 1;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.cassette = (CassetteState){.count = 1};
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.cassette = (CassetteState){.split = 1};
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.cassette = (CassetteState){.slot = 1};
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.cassette = (CassetteState){.side = 1};
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.cassette = (CassetteState){.flip = true};
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));

    state = cassette_session();
    state.queue_kind = SESSION_QUEUE_GENERATED;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.slot = 32;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.side = 2;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.split = 0;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.split = state.cassette.count;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.count = CASSETTE_SIDE_CAPACITY + 2;
    state.cassette.split = CASSETTE_SIDE_CAPACITY + 1;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.count = CASSETTE_SIDE_CAPACITY + 5;
    state.cassette.split = 4;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.resume.cursor.position = state.cassette.count;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.flip = true;
    state.resume.cursor.position = 0;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.resume.cursor.position = state.cassette.split - 1;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    state = cassette_session();
    state.cassette.side = 1;
    state.cassette.flip = true;
    state.resume.cursor.position = state.cassette.count - 1;
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_session_and_its_pending_songs_are_saved_together),
        TEST_CASE(torn_writes_preserve_a_complete_session),
        TEST_CASE(failed_readback_requires_reload_before_another_write),
        TEST_CASE(previous_session_is_available_and_corrupt_newest_is_ignored),
        TEST_CASE(future_versions_are_preserved_and_invalid_ids_never_write),
        TEST_CASE(stored_generation_zero_and_serial_wrap_are_supported),
        TEST_CASE(a_session_of_another_library_reads_as_empty_and_is_replaced),
        TEST_CASE(a_refused_write_is_a_definite_failure_that_keeps_both_records),
        TEST_CASE(a_record_is_one_block_with_its_payload_inside),
        TEST_CASE(invalid_configurations_and_queue_states_fail_closed),
        TEST_CASE(invalid_cassette_states_are_never_persisted),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
