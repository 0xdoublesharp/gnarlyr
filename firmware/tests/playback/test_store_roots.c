#include <string.h>

#include "check.h"
#include "playback/mix_tape.h"
#include "playback/session_journal.h"
#include "playback/track_order.h"

enum {
    FINGERPRINT = 0x5eed1234,
    TRACK_LIMIT = 32,
    SLOT = 7,
    TAPE_BYTES = 2048,
    NO_CUT = 0,
    EDIT_WRITES = 5,
    SAVE_WRITES = 17
};

typedef struct Disk {
    uint8_t *bytes;
    uint32_t size;
} Disk;

static uint8_t journal_bytes[SESSION_FILE_BYTES];
static uint8_t order_bytes[TRACK_ORDER_FILE_BYTES];
static uint8_t catalog_bytes[MIX_CATALOG_BYTES];
static uint8_t tape_bytes[MIX_TAPE_BANKS][TAPE_BYTES];
static Disk journal_disk = {journal_bytes, sizeof journal_bytes};
static Disk order_disk = {order_bytes, sizeof order_bytes};
static Disk catalog_disk = {catalog_bytes, sizeof catalog_bytes};
static Disk tape_disks[MIX_TAPE_BANKS] = {{tape_bytes[0], TAPE_BYTES}, {tape_bytes[1], TAPE_BYTES}};
static int writes;
static int cut_at;
static bool power;

static uint16_t first_order[] = {7, 3, 7, 9};
static const uint16_t MOVED[] = {9, 7, 3, 7};
static const uint16_t REMOVED[] = {9, 3, 7};
static MixIdentity songs[] = {
    {.path = "A/one.flac"},
    {.path = "A/two.flac", .start_ms = 1000, .end_ms = 2000},
    {.path = "B/three.flac"},
    {.path = "A/one.flac"},
};

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const Disk *disk = context;
    memcpy(bytes, disk->bytes + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const Disk *disk = context;
    if (!power) {
        return false;
    }
    if (++writes == cut_at) {
        memcpy(disk->bytes + offset, bytes, length / 2);
        power = false;
        return false;
    }
    memcpy(disk->bytes + offset, bytes, length);
    return true;
}

static StoreFile file_of(Disk *disk)
{
    return (StoreFile){.source = {read_disk, disk}, .write = write_disk, .size = disk->size};
}

static void restart(int cut)
{
    writes = 0;
    cut_at = cut;
    power = true;
}

static SessionJournalConfig journal_config(void)
{
    return (SessionJournalConfig){
        .file = file_of(&journal_disk), .fingerprint = FINGERPRINT, .track_limit = TRACK_LIMIT};
}

static TrackOrderConfig order_config(void)
{
    return (TrackOrderConfig){
        .file = file_of(&order_disk), .fingerprint = FINGERPRINT, .track_limit = TRACK_LIMIT};
}

static bool read_tracks(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    const uint16_t *source = context;
    memcpy(tracks, source + first, (size_t)count * sizeof *tracks);
    return true;
}

static bool edit_order(TrackOrder *order, TrackOrderSource source, TrackOrderEdit edit)
{
    TrackOrderJob job;
    StoreResult result = STORE_FAILED;
    if (track_order_job_begin(&job, order, source, edit)) {
        do {
            result = track_order_job_step(&job);
        } while (result == STORE_WORKING);
    }
    return result == STORE_DONE;
}

static bool save_session(SessionJournal *journal, const TrackOrder *order)
{
    SessionState state = {.resume = {.library_fingerprint = FINGERPRINT,
                                     .plan = {.source = {.kind = BROWSE_ALBUM_TRACKS,
                                                         .group = LIBRARY_ARTISTS,
                                                         .artist = LIBRARY_NONE}},
                                     .cursor = {.position = 1}},
                          .order_generation = order->generation,
                          .queue_kind = SESSION_QUEUE_STORED};
    return track_order_read(order, 1, 1, &state.resume.track) &&
           session_journal_save(journal, &state) == STORE_DONE;
}

static void make_an_edited_queue(void)
{
    SessionJournal journal;
    SessionState state;
    TrackOrder order;
    memset(journal_bytes, 0, sizeof journal_bytes);
    memset(order_bytes, 0xa5, sizeof order_bytes);
    memset(order_bytes, 0, 4096);
    restart(NO_CUT);
    CHECK_EQUAL(STORE_EMPTY, session_journal_open(&journal, journal_config(), &state));
    CHECK_EQUAL(STORE_EMPTY, track_order_open(&order, order_config()));
    TrackOrderSource source = {read_tracks, first_order, 4};
    CHECK(edit_order(&order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK(save_session(&journal, &order));
    CHECK(edit_order(&order, track_order_source(&order),
                     (TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    CHECK(save_session(&journal, &order));
}

static bool edit_the_queue(bool checkpoint)
{
    SessionJournal journal;
    SessionState state;
    TrackOrder order;
    if (session_journal_open(&journal, journal_config(), &state) != STORE_READY ||
        track_order_open_at(&order, order_config(), state.order_generation) != STORE_READY) {
        return false;
    }
    if (checkpoint && !save_session(&journal, &order)) {
        return false;
    }
    return edit_order(&order, track_order_source(&order),
                      (TrackOrderEdit){TRACK_ORDER_REMOVE, 1, 0, 0}) &&
           save_session(&journal, &order);
}

static bool session_plays(const SessionState *state)
{
    TrackOrder order;
    uint16_t tracks[4] = {0};
    if (track_order_open_at(&order, order_config(), state->order_generation) != STORE_READY) {
        return false;
    }
    const uint16_t *expected = order.count == 3 ? REMOVED : MOVED;
    if (order.count == 4 && state->order_generation == 1) {
        expected = first_order;
    }
    return track_order_read(&order, 0, order.count, tracks) &&
           memcmp(tracks, expected, order.count * sizeof *tracks) == 0 &&
           state->resume.track == tracks[state->resume.cursor.position];
}

static void an_edited_queue_is_whole_after_a_cut_at_any_write(void)
{
    for (int cut = NO_CUT; cut <= EDIT_WRITES; cut++) {
        make_an_edited_queue();
        restart(cut);
        bool edited = edit_the_queue(true);
        CHECK_EQUAL(cut == NO_CUT, edited);
        CHECK_EQUAL(cut == NO_CUT ? EDIT_WRITES : cut, writes);
        restart(NO_CUT);
        SessionJournal journal;
        SessionState state;
        CHECK_EQUAL(STORE_READY, session_journal_open(&journal, journal_config(), &state));
        CHECK(session_plays(&state));
        CHECK_EQUAL(edited ? 3 : 2, state.order_generation);
        if (session_journal_previous(&journal, &state) == STORE_READY) {
            CHECK(session_plays(&state));
        }
        CHECK(edit_the_queue(true));
    }
}

static void without_the_extra_save_the_older_record_loses_its_order(void)
{
    make_an_edited_queue();
    restart(2);
    CHECK(!edit_the_queue(false));
    restart(NO_CUT);
    SessionJournal journal;
    SessionState state;
    CHECK_EQUAL(STORE_READY, session_journal_open(&journal, journal_config(), &state));
    CHECK(session_plays(&state));
    CHECK_EQUAL(STORE_READY, session_journal_previous(&journal, &state));
    CHECK_EQUAL(1, state.order_generation);
    CHECK(!session_plays(&state));
}

static bool read_song(void *context, uint16_t index, MixIdentity *identity)
{
    const MixIdentity *source = context;
    *identity = source[index];
    return true;
}

static bool finish_catalog(MixCatalog *catalog, MixEntry entry)
{
    MixCatalogJob job;
    StoreResult result = STORE_FAILED;
    if (mix_catalog_begin(&job, catalog, catalog->generation, (MixPatch){entry, SLOT})) {
        do {
            result = mix_catalog_step(&job);
        } while (result == STORE_WORKING);
    }
    return result == STORE_DONE;
}

static bool write_tape(MixEntry *entry, uint16_t count)
{
    MixMeasure measured;
    MixTapeWrite writer;
    StoreResult result = STORE_FAILED;
    mix_measure_begin(&measured, (MixSource){read_song, songs, count});
    while (mix_measure_step(&measured) == STORE_WORKING) {
    }
    entry->file_bank = entry->active ? (uint8_t)(1 - entry->file_bank) : 0;
    entry->generation++;
    entry->count = count;
    entry->bytes = measured.bytes;
    entry->active = true;
    strcpy(entry->name, "Side A");
    StoreFile file = file_of(&tape_disks[entry->file_bank]);
    if (mix_tape_write_begin(&writer, file, &measured, entry->generation)) {
        do {
            result = mix_tape_write_step(&writer);
        } while (result == STORE_WORKING);
    }
    return result == STORE_DONE;
}

static bool save_mixtape(uint16_t count, bool checkpoint)
{
    MixCatalog catalog;
    MixEntry entry;
    StoreLoad load = mix_catalog_open(&catalog, file_of(&catalog_disk));
    if ((load != STORE_READY && load != STORE_EMPTY) || !mix_catalog_read(&catalog, SLOT, &entry)) {
        return false;
    }
    if (checkpoint && !finish_catalog(&catalog, entry)) {
        return false;
    }
    return write_tape(&entry, count) && finish_catalog(&catalog, entry);
}

static void make_a_mixtape_saved_twice(void)
{
    memset(catalog_bytes, 0, sizeof catalog_bytes);
    memset(tape_bytes, 0, sizeof tape_bytes);
    restart(NO_CUT);
    CHECK(save_mixtape(1, true));
    CHECK(save_mixtape(2, true));
}

static bool mixtape_plays(const MixCatalog *catalog)
{
    MixEntry entry;
    MixTape tape;
    MixTapeCheck check;
    MixTapeCursor cursor;
    MixIdentity identity;
    if (!mix_catalog_read(catalog, SLOT, &entry) || !entry.active) {
        return false;
    }
    mix_tape_check_begin(&check, &tape, file_of(&tape_disks[entry.file_bank]), entry.generation);
    while (mix_tape_check_step(&check) == STORE_WORKING) {
    }
    if (check.result != STORE_DONE || tape.count != entry.count || tape.bytes != entry.bytes) {
        return false;
    }
    mix_tape_cursor(&cursor, &tape);
    for (uint16_t index = 0; index < tape.count; index++) {
        if (mix_tape_next(&cursor, &identity) != STORE_READY ||
            !mix_identity_equal(&identity, &songs[index])) {
            return false;
        }
    }
    return mix_tape_next(&cursor, &identity) == STORE_EMPTY;
}

static void a_saved_mixtape_is_whole_after_a_cut_at_any_write(void)
{
    for (int cut = NO_CUT; cut <= SAVE_WRITES; cut++) {
        make_a_mixtape_saved_twice();
        restart(cut);
        bool saved = save_mixtape(4, true);
        CHECK_EQUAL(cut == NO_CUT || cut == SAVE_WRITES, saved);
        CHECK_EQUAL(cut == NO_CUT ? SAVE_WRITES : cut, writes);
        restart(NO_CUT);
        MixCatalog catalog;
        MixEntry entry;
        CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, file_of(&catalog_disk)));
        CHECK(mixtape_plays(&catalog));
        CHECK(mix_catalog_read(&catalog, SLOT, &entry));
        CHECK_EQUAL(saved ? 4 : 2, entry.count);
        MixCatalog older = catalog;
        if (mix_catalog_previous(&older) == STORE_READY) {
            CHECK(mixtape_plays(&older));
        }
        CHECK(save_mixtape(3, true));
        CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, file_of(&catalog_disk)));
        CHECK(mixtape_plays(&catalog));
    }
}

static void without_the_extra_publication_the_older_catalogue_loses_its_tape(void)
{
    make_a_mixtape_saved_twice();
    restart(2);
    CHECK(!save_mixtape(4, false));
    restart(NO_CUT);
    MixCatalog catalog;
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, file_of(&catalog_disk)));
    CHECK(mixtape_plays(&catalog));
    CHECK_EQUAL(STORE_READY, mix_catalog_previous(&catalog));
    CHECK(!mixtape_plays(&catalog));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_edited_queue_is_whole_after_a_cut_at_any_write),
        TEST_CASE(without_the_extra_save_the_older_record_loses_its_order),
        TEST_CASE(a_saved_mixtape_is_whole_after_a_cut_at_any_write),
        TEST_CASE(without_the_extra_publication_the_older_catalogue_loses_its_tape),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
