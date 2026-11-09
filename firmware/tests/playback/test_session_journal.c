#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "playback/session_journal.h"

enum {
    FINGERPRINT = 0x12345678,
    TRACK_LIMIT = 10
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
    return (SessionJournalConfig){.source = {read_disk, NULL},
                                  .write = write_disk,
                                  .size = sizeof disk,
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
    CHECK_EQUAL(SESSION_EMPTY, session_journal_open(&journal, configuration(), &restored));
    SessionState state = sample();
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
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
    CHECK_EQUAL(SESSION_EMPTY, session_journal_open(&journal, configuration(), &restored));
}

static void put32(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static void rehash(uint8_t *bytes)
{
    uint32_t hash = hash_update(HASH_START, bytes, 12);
    hash = hash_update(hash, bytes + 16, 268);
    put32(bytes + 12, hash);
}

static void torn_writes_preserve_a_complete_session(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    uint8_t before[SESSION_FILE_BYTES];
    memcpy(before, disk, sizeof before);
    for (uint32_t cut = 0; cut <= SESSION_RECORD_BYTES; cut++) {
        memcpy(disk, before, sizeof before);
        SessionState restored;
        CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
        state.resume.position_ms = 98765;
        up_next_clear(&state.up_next);
        write_bytes = cut;
        write_result = false;
        SessionSave result = session_journal_save(&journal, &state);
        CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
        CHECK_EQUAL(result == SESSION_SAVED ? 98765 : 12345, restored.resume.position_ms);
        CHECK_EQUAL(result == SESSION_SAVED ? 0 : 1, restored.up_next.count);
        CHECK(memcmp(before, disk, SESSION_RECORD_BYTES) == 0);
    }
}

static void failed_readback_requires_reload_before_another_write(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    state.resume.position_ms = 4444;
    refuse_after_write = true;
    CHECK_EQUAL(SESSION_SAVE_UNCERTAIN, session_journal_save(&journal, &state));
    CHECK(journal.needs_reload);
    refuse_reads = false;
    refuse_after_write = false;
    CHECK_EQUAL(SESSION_SAVE_FAILED, session_journal_save(&journal, &state));
    SessionState restored;
    CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(4444, restored.resume.position_ms);
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
}

static void previous_session_is_available_and_corrupt_newest_is_ignored(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    state.resume.position_ms = 5555;
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    SessionState restored;
    CHECK_EQUAL(SESSION_READY, session_journal_previous(&journal, &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
    disk[SESSION_RECORD_BYTES + 48] ^= 1;
    CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(12345, restored.resume.position_ms);
    disk[48] ^= 1;
    CHECK_EQUAL(SESSION_CORRUPT, session_journal_open(&journal, configuration(), &restored));
}

static void future_versions_are_preserved_and_invalid_ids_never_write(void)
{
    reset();
    SessionState state = sample();
    state.up_next.count = UP_NEXT_CAPACITY + 1;
    CHECK_EQUAL(SESSION_SAVE_FAILED, session_journal_save(&journal, &state));
    state = sample();
    state.up_next.tracks[0] = TRACK_LIMIT;
    CHECK_EQUAL(SESSION_SAVE_FAILED, session_journal_save(&journal, &state));
    state = sample();
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    disk[4] = 2;
    rehash(disk);
    SessionState restored;
    CHECK_EQUAL(SESSION_UNSUPPORTED, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(SESSION_SAVE_FAILED, session_journal_save(&journal, &state));
    CHECK_EQUAL(2, disk[4]);
}

static void stored_generation_zero_and_serial_wrap_are_supported(void)
{
    reset();
    SessionState state = sample();
    state.queue_kind = SESSION_QUEUE_STORED;
    up_next_clear(&state.up_next);
    state.order_generation = 0;
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    put32(disk + 8, UINT32_MAX);
    rehash(disk);
    SessionState restored;
    CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(UINT32_MAX, journal.generation);
    state.resume.position_ms = 6666;
    CHECK_EQUAL(SESSION_SAVED, session_journal_save(&journal, &state));
    CHECK_EQUAL(0, journal.generation);
    CHECK_EQUAL(SESSION_READY, session_journal_open(&journal, configuration(), &restored));
    CHECK_EQUAL(6666, restored.resume.position_ms);
    CHECK_EQUAL(0, restored.order_generation);
    CHECK_EQUAL(SESSION_QUEUE_STORED, restored.queue_kind);
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
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
