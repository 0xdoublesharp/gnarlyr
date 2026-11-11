#include <string.h>

#include "check.h"
#include "playback/session_journal.h"

static uint8_t disk[SESSION_FILE_BYTES];
static uint8_t saved[SESSION_FILE_BYTES];
static uint32_t cut;
static bool unreadable;
static bool lose_readback;
static SessionJournal journal;

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (unreadable || offset > sizeof disk || length > sizeof disk - offset) {
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
    memcpy(disk + offset, bytes, cut < length ? cut : length);
    unreadable = lose_readback;
    return cut >= length;
}

static SessionJournalConfig config(void)
{
    return (SessionJournalConfig){
        .file = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk},
        .fingerprint = 7,
        .track_limit = 8};
}

static SessionState sample(void)
{
    return (SessionState){
        .resume = {.library_fingerprint = 7,
                   .plan = {.source = {.kind = BROWSE_ALL_SONGS, .group = LIBRARY_ARTISTS},
                            .anchor_track = 0},
                   .volume = 12},
        .order_generation = 3,
        .queue_kind = SESSION_QUEUE_STORED,
        .cassette = {
            .generation = 9, .count = 3, .split = 1, .slot = 2, .active = true, .flip = true}};
}

static void reset(void)
{
    memset(disk, 0, sizeof disk);
    cut = STORE_BLOCK_BYTES;
    unreadable = false;
    lose_readback = false;
    SessionState state;
    CHECK_EQUAL(STORE_EMPTY, session_journal_open(&journal, config(), &state));
}

static void torn_flip_publications_keep_side_entry_and_position_together(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    memcpy(saved, disk, sizeof disk);
    state.cassette.side = 1;
    state.cassette.flip = false;
    state.resume.cursor.position = 1;
    state.resume.track = 1;
    state.resume.position_ms = 12345;
    for (uint32_t amount = 0; amount <= STORE_BLOCK_BYTES; amount++) {
        memcpy(disk, saved, sizeof disk);
        cut = amount;
        SessionState restored;
        CHECK_EQUAL(STORE_READY, session_journal_open(&journal, config(), &restored));
        StoreResult result = session_journal_save(&journal, &state);
        CHECK_EQUAL(STORE_READY, session_journal_open(&journal, config(), &restored));
        bool newer = result == STORE_DONE;
        CHECK_EQUAL(newer ? 1 : 0, restored.cassette.side);
        CHECK_EQUAL(!newer, restored.cassette.flip);
        CHECK_EQUAL(newer ? 1 : 0, restored.resume.cursor.position);
        CHECK_EQUAL(newer ? 12345 : 0, restored.resume.position_ms);
        CHECK_EQUAL(9, restored.cassette.generation);
        CHECK_EQUAL(2, restored.cassette.slot);
    }
}

static void uncertain_flip_requires_reopening_and_invalid_side_positions_never_publish(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    state.cassette.side = 1;
    state.resume.cursor.position = 1;
    state.resume.track = 1;
    state.cassette.flip = false;
    lose_readback = true;
    CHECK_EQUAL(STORE_UNCERTAIN, session_journal_save(&journal, &state));
    lose_readback = false;
    unreadable = false;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    SessionState restored;
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, config(), &restored));
    CHECK_EQUAL(1, restored.cassette.side);
    state.resume.cursor.position = 0;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
    state.resume.cursor.position = 1;
    state.cassette.split = 0;
    CHECK_EQUAL(STORE_FAILED, session_journal_save(&journal, &state));
}

static void an_unrecognized_payload_shape_does_not_restore_partial_state(void)
{
    reset();
    SessionState state = sample();
    CHECK_EQUAL(STORE_DONE, session_journal_save(&journal, &state));
    StoreKind kind = {.magic = {'D', 'S', 'S', 'J'}, .version = 1, .inline_payload = true};
    StoreHeader header = {.generation = 1, .bytes = 268, .binding = 7};
    store_encode(&kind, &header, disk);
    SessionState restored = {.resume.position_ms = 777};
    CHECK_EQUAL(STORE_CORRUPT, session_journal_open(&journal, config(), &restored));
    CHECK_EQUAL(777, restored.resume.position_ms);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(torn_flip_publications_keep_side_entry_and_position_together),
        TEST_CASE(uncertain_flip_requires_reopening_and_invalid_side_positions_never_publish),
        TEST_CASE(an_unrecognized_payload_shape_does_not_restore_partial_state),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
