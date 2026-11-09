#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "device/fat.h"
#include "fixture_library.h"
#include "playback/play_queue.h"
#include "playback/track_order.h"
#include "sparse_card.h"

enum {
    HELLS_BELLS = 0,
    SHOOT_TO_THRILL = 1,
    HELP = 2,
    ARMY_OF_ME = 3
};

static const char PATH[] = "/.echo/order.bin";
static const uint16_t TRACKS[] = {HELP, HELLS_BELLS, ARMY_OF_ME, HELP};

typedef struct StoredFile {
    SparseCard card;
    FatVolume volume;
    FatFile file;
    TrackOrder order;
} StoredFile;

static StoredFile stored;
static FixtureLibrary fixture;
static bool fail_reads;
static bool fail_writes;
static bool (*write_sectors)(void *, uint32_t, uint32_t, const void *);

static bool disk_write(void *context, uint32_t sector, uint32_t count, const void *bytes)
{
    return !fail_writes && write_sectors(context, sector, count, bytes);
}

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    StoredFile *file = context;
    return !fail_reads && fat_read(&file->volume, &file->file, offset, bytes, length);
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    StoredFile *file = context;
    return fat_write(&file->volume, &file->file, offset, bytes, length);
}

static TrackOrderConfig configuration(void)
{
    return (TrackOrderConfig){.source = {read_file, &stored},
                              .write = write_file,
                              .size = stored.file.size,
                              .fingerprint = library_fingerprint(&fixture.library),
                              .track_limit = library_count(&fixture.library, LIBRARY_TRACKS)};
}

static void mount(void)
{
    BlockDevice device = sparse_card_device(&stored.card);
    write_sectors = device.write;
    device.write = disk_write;
    CHECK(fat_mount(&stored.volume, device));
}

static void start(const char *name)
{
    fail_reads = false;
    fail_writes = false;
    fixture_library_open(&fixture);
    CHECK(sparse_card_load(&stored.card, name));
    mount();
    CHECK_EQUAL(FAT_RESERVE_CREATED, fat_reserve(&stored.volume, PATH, TRACK_ORDER_FILE_BYTES));
    CHECK(fat_open(&stored.volume, PATH, &stored.file));
    CHECK_EQUAL(TRACK_ORDER_FILE_BYTES, stored.file.size);
    CHECK_EQUAL(TRACK_ORDER_EMPTY, track_order_open(&stored.order, configuration()));
}

static bool read_tracks(const void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    const uint16_t *source = context;
    memcpy(tracks, source + first, (size_t)count * sizeof *tracks);
    return true;
}

static TrackOrderResult save(TrackOrderSource source, TrackOrderEdit edit)
{
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &stored.order, source, edit));
    int steps = 0;
    TrackOrderResult result;
    do {
        result = track_order_job_step(&job);
        CHECK(++steps < 1024);
    } while (result == TRACK_ORDER_WORKING);
    return result;
}

static void copy_tracks(void)
{
    TrackOrderSource source = {read_tracks, TRACKS, 4};
    CHECK_EQUAL(TRACK_ORDER_DONE, save(source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
}

static PlayQueuePlan plan(void)
{
    return (PlayQueuePlan){.source = {.kind = BROWSE_ALBUM_TRACKS,
                                      .group = LIBRARY_ARTISTS,
                                      .subject = 0,
                                      .artist = LIBRARY_NONE},
                           .anchor_track = HELLS_BELLS,
                           .shuffle = SHUFFLE_ALBUMS,
                           .seed = 7};
}

static PlayQueue bound_queue(void)
{
    PlayQueue queue;
    CHECK(play_queue_use_order(&queue, &fixture.library, &stored.order, plan()));
    return queue;
}

static void stored_queues_retain_the_explicit_order_duplicates_and_repeat_rules(void)
{
    start("fat32_mbr");
    copy_tracks();
    PlayQueue queue = bound_queue();
    for (uint16_t index = 0; index < 4; index++) {
        uint16_t track = UINT16_MAX;
        CHECK(play_queue_track_at(&queue, (PlayQueueCursor){index, 0}, &track));
        CHECK_EQUAL(TRACKS[index], track);
    }
    CHECK_EQUAL(4, queue.count);
    PlayQueueCursor next;
    CHECK(play_queue_upcoming(&queue, REPEAT_ONE, &next));
    CHECK_EQUAL(0, next.position);
    play_queue_move_to(&queue, (PlayQueueCursor){3, 0});
    CHECK(!play_queue_upcoming(&queue, REPEAT_OFF, &next));
    CHECK(play_queue_upcoming(&queue, REPEAT_ALL, &next));
    CHECK_EQUAL(0, next.position);
    CHECK(play_queue_neighbor(&queue, -1, false, &next));
    CHECK_EQUAL(2, next.position);
    PlayQueuePlace place = play_queue_place(&queue);
    CHECK_EQUAL(4, place.number);
    CHECK_EQUAL(4, place.total);
    sparse_card_free(&stored.card);
}

static void a_new_commit_invalidates_old_queue_views_until_rebound(void)
{
    start("fat32_mbr");
    copy_tracks();
    PlayQueue queue = bound_queue();
    CHECK_EQUAL(TRACK_ORDER_DONE, save(track_order_source(&stored.order),
                                       (TrackOrderEdit){TRACK_ORDER_MOVE, 2, 0, 0}));
    uint16_t track = 0;
    PlayQueueCursor next;
    CHECK(!play_queue_current(&queue, &track));
    CHECK(!play_queue_upcoming(&queue, REPEAT_ONE, &next));
    CHECK(!play_queue_neighbor(&queue, 1, false, &next));
    CHECK_EQUAL(0, play_queue_place(&queue).total);
    CHECK(play_queue_use_order(&queue, &fixture.library, &stored.order, plan()));
    CHECK(play_queue_current(&queue, &track));
    CHECK_EQUAL(ARMY_OF_ME, track);
    sparse_card_free(&stored.card);
}

static void read_failures_and_invalid_cursors_do_not_return_invented_tracks(void)
{
    start("fat32_mbr");
    copy_tracks();
    PlayQueue queue = bound_queue();
    uint16_t track = 0;
    PlayQueueCursor next;
    CHECK(!play_queue_track_at(&queue, (PlayQueueCursor){4, 0}, &track));
    CHECK(!play_queue_track_at(&queue, (PlayQueueCursor){0, 1}, &track));
    CHECK(!play_queue_step(&queue, (PlayQueueCursor){0, 1}, 1, false, &next));
    fail_reads = true;
    CHECK(!play_queue_current(&queue, &track));
    fail_reads = false;
    CHECK(play_queue_current(&queue, &track));
    CHECK_EQUAL(HELP, track);
    sparse_card_free(&stored.card);
}

static void another_library_is_rejected_without_replacing_the_current_queue(void)
{
    start("fat32_mbr");
    copy_tracks();
    PlayQueuePlan original = plan();
    original.shuffle = SHUFFLE_OFF;
    PlayQueue queue;
    CHECK(play_queue_build(&queue, &fixture.library, original));
    stored.order.config.fingerprint++;
    CHECK(!play_queue_use_order(&queue, &fixture.library, &stored.order, original));
    uint16_t track = UINT16_MAX;
    CHECK(play_queue_current(&queue, &track));
    CHECK_EQUAL(HELLS_BELLS, track);
    sparse_card_free(&stored.card);
}

static void rebuilding_a_generated_queue_releases_the_stored_binding(void)
{
    start("fat32_mbr");
    copy_tracks();
    PlayQueue queue = bound_queue();
    PlayQueuePlan original = plan();
    original.shuffle = SHUFFLE_OFF;
    CHECK(play_queue_build(&queue, &fixture.library, original));
    uint16_t track = UINT16_MAX;
    CHECK(play_queue_current(&queue, &track));
    CHECK_EQUAL(HELLS_BELLS, track);
    CHECK_EQUAL(2, queue.count);
    CHECK(queue.stored_order == NULL);
    sparse_card_free(&stored.card);
}

static void failed_fat_writes_preserve_the_previous_committed_sequence(void)
{
    start("fat32_mbr");
    copy_tracks();
    fail_writes = true;
    CHECK_EQUAL(TRACK_ORDER_FAILED, save(track_order_source(&stored.order),
                                         (TrackOrderEdit){TRACK_ORDER_REMOVE, 0, 0, 0}));
    fail_writes = false;
    mount();
    CHECK(fat_open(&stored.volume, PATH, &stored.file));
    CHECK_EQUAL(TRACK_ORDER_READY, track_order_open(&stored.order, configuration()));
    uint16_t tracks[4] = {0};
    CHECK(track_order_read(&stored.order, 0, 4, tracks));
    CHECK(memcmp(tracks, TRACKS, sizeof tracks) == 0);
    sparse_card_free(&stored.card);
}

static bool generated(const void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    (void)context;
    uint32_t limit = library_count(&fixture.library, LIBRARY_TRACKS);
    for (uint32_t index = 0; index < count; index++) {
        tracks[index] = (uint16_t)((first + index) % limit);
    }
    return true;
}

static void full_capacity_survives_remount(const char *name)
{
    start(name);
    TrackOrderSource source = {generated, NULL, TRACK_ORDER_CAPACITY};
    CHECK_EQUAL(TRACK_ORDER_DONE, save(source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(TRACK_ORDER_DONE,
                save(track_order_source(&stored.order),
                     (TrackOrderEdit){TRACK_ORDER_MOVE, TRACK_ORDER_CAPACITY - 1, 0, 0}));
    mount();
    CHECK(fat_open(&stored.volume, PATH, &stored.file));
    CHECK_EQUAL(TRACK_ORDER_READY, track_order_open(&stored.order, configuration()));
    CHECK_EQUAL(TRACK_ORDER_CAPACITY, stored.order.count);
    uint16_t track = UINT16_MAX;
    CHECK(track_order_read(&stored.order, 0, 1, &track));
    CHECK_EQUAL((TRACK_ORDER_CAPACITY - 1) % library_count(&fixture.library, LIBRARY_TRACKS),
                track);
    CHECK_EQUAL(TRACK_ORDER_DONE, save(track_order_source(&stored.order),
                                       (TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
    mount();
    CHECK(fat_open(&stored.volume, PATH, &stored.file));
    CHECK_EQUAL(TRACK_ORDER_READY, track_order_open(&stored.order, configuration()));
    PlayQueue empty = bound_queue();
    CHECK_EQUAL(0, empty.count);
    CHECK(!play_queue_current(&empty, &track));
    sparse_card_free(&stored.card);
}

static void full_capacity_orders_persist_on_fat32(void)
{
    full_capacity_survives_remount("fat32_mbr");
}

static void full_capacity_orders_persist_on_fat16(void)
{
    full_capacity_survives_remount("fat16_superfloppy");
}

static void full_capacity_orders_persist_on_exfat(void)
{
    full_capacity_survives_remount("exfat_mbr");
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(stored_queues_retain_the_explicit_order_duplicates_and_repeat_rules),
        TEST_CASE(a_new_commit_invalidates_old_queue_views_until_rebound),
        TEST_CASE(read_failures_and_invalid_cursors_do_not_return_invented_tracks),
        TEST_CASE(another_library_is_rejected_without_replacing_the_current_queue),
        TEST_CASE(rebuilding_a_generated_queue_releases_the_stored_binding),
        TEST_CASE(failed_fat_writes_preserve_the_previous_committed_sequence),
        TEST_CASE(full_capacity_orders_persist_on_fat32),
        TEST_CASE(full_capacity_orders_persist_on_fat16),
        TEST_CASE(full_capacity_orders_persist_on_exfat),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
