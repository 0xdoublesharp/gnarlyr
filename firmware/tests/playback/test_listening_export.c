#include <string.h>

#include "check.h"
#include "listening_fixture.h"

enum {
    MONDAY = LISTENING_FIXTURE_MONDAY,
    STEP_LIMIT = 100000,
    TEXT_CAPACITY = 16384
};

#define HEADER "#AUDIOSCROBBLER/1.1\n#TZ/UNKNOWN\n#CLIENT/" FIRMWARE_NAME " " FIRMWARE_VERSION "\n"
#define HUNTER "Björk\tHomogenic\tHunter\t1\t254\tL\t1794182400\t\n"
#define JOGA_SKIPPED "Björk\tHomogenic\tJóga\t2\t305\tS\t1794182700\t\n"
#define PAD_LINE "#                                                              \n"
#define ROADS                                                                                      \
    "Portishead\tDummy\tRoads\t8\t305\tL\t1794268800\t0f1e2d3c-4b5a-6978-8695-a4b3c2d1e0f9\n"

static ListeningDisk disk;
static ListeningDisk target;
static ListeningLog log_file;
static ListeningExport job;
static char long_artist[LISTENING_TEXT_BYTES];

static void reset(void)
{
    listening_disk_reset(&disk);
    listening_disk_reset(&target);
    memset(target.bytes, 0xee, TEXT_CAPACITY);
    CHECK_EQUAL(STORE_EMPTY, listening_log_open(&log_file, listening_disk_config(&disk)));
}

static void add(ListeningPlay play)
{
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
}

static void add_three_plays(void)
{
    add(listening_fixture_dated(0, MONDAY, 0));
    ListeningPlay skipped = listening_fixture_dated(1, MONDAY, 300);
    skipped.listened_ms = 20000;
    skipped.end = LISTENING_SKIPPED;
    add(skipped);
    add(listening_fixture_dated(3, MONDAY + 1, 0));
}

static StoreResult run(void)
{
    for (int step = 0; step < STEP_LIMIT && job.result == STORE_WORKING; step++) {
        listening_export_step(&job);
    }
    return job.result;
}

static uint32_t measure(ListeningLookup lookup)
{
    listening_export_begin(&job, &log_file, lookup);
    CHECK_EQUAL(STORE_DONE, run());
    return job.bytes;
}

static StoreResult export_to(uint32_t size)
{
    measure(listening_fixture_lookup());
    CHECK(listening_export_write(&job, listening_disk_file(&target, size)));
    return run();
}

static void check_text(const char *wanted)
{
    uint32_t length = (uint32_t)strlen(wanted);
    CHECK(memcmp(wanted, target.bytes, length) == 0);
    CHECK_EQUAL(0xee, target.bytes[length]);
}

static void the_log_is_exported_as_a_scrobbler_file_byte_for_byte(void)
{
    static const char WANTED[] = HEADER HUNTER JOGA_SKIPPED ROADS;
    reset();
    add_three_plays();
    CHECK_EQUAL(sizeof WANTED - 1, measure(listening_fixture_lookup()));
    CHECK_EQUAL(0, target.writes);
    CHECK_EQUAL(STORE_DONE, export_to(sizeof WANTED - 1));
    check_text(WANTED);
    CHECK_EQUAL(3, job.exported);
    CHECK_EQUAL(0, job.unknown);
    CHECK_EQUAL(0, job.dropped);
    CHECK_EQUAL(1, target.writes);
    CHECK(memcmp(LISTENING_EXPORT_PATH, "/.scrobbler.log", 16) == 0);
}

static void without_a_clock_the_timestamp_is_zero(void)
{
    static const char WANTED[] = HEADER "Björk\tHomogenic\tHunter\t1\t254\tL\t0\t\n"
                                        "Portishead\tDummy\tGlory Box\t11\t208\tS\t0\t\n";
    reset();
    add(listening_fixture_play(0, 254000));
    add(listening_fixture_play(4, 103999));
    CHECK_EQUAL(STORE_DONE, export_to(sizeof WANTED - 1));
    check_text(WANTED);
}

static void tabs_and_line_breaks_in_a_tag_become_spaces_and_missing_fields_stay_empty(void)
{
    static const char WANTED[] = HEADER "Low\t\tTab bed room\t\t0\tS\t0\t\n";
    reset();
    add(listening_fixture_play(LISTENING_FIXTURE_UNTITLED, 61000));
    CHECK_EQUAL(STORE_DONE, export_to(sizeof WANTED - 1));
    check_text(WANTED);
}

static void the_rest_of_a_larger_file_is_comment_lines(void)
{
    static const char WANTED[] = HEADER HUNTER JOGA_SKIPPED ROADS PAD_LINE PAD_LINE "#\n";
    reset();
    add_three_plays();
    CHECK_EQUAL(LISTENING_PAD_LINE_BYTES, sizeof PAD_LINE - 1);
    CHECK_EQUAL(STORE_DONE, export_to(sizeof WANTED - 1));
    check_text(WANTED);
    reset();
    add_three_plays();
    CHECK_EQUAL(STORE_DONE, export_to(sizeof HEADER HUNTER JOGA_SKIPPED ROADS));
    check_text(HEADER HUNTER JOGA_SKIPPED ROADS "#");
}

static void a_file_of_several_sectors_is_written_whole_sectors_first(void)
{
    reset();
    for (int index = 0; index < 40; index++) {
        add(listening_fixture_dated(0, MONDAY, 0));
    }
    uint32_t text = sizeof HEADER - 1 + 40 * (sizeof HUNTER - 1);
    uint32_t size = 5 * STORE_BLOCK_BYTES + 100;
    CHECK_EQUAL(STORE_DONE, export_to(size));
    CHECK(memcmp(HEADER HUNTER HUNTER, target.bytes, sizeof HEADER HUNTER HUNTER - 1) == 0);
    CHECK(memcmp(HUNTER "#   ", target.bytes + text - (sizeof HUNTER - 1), sizeof HUNTER + 3) == 0);
    CHECK_EQUAL('\n', target.bytes[size - 1]);
    CHECK_EQUAL(0xee, target.bytes[size]);
    CHECK_EQUAL(6, target.writes);
    CHECK_EQUAL(STORE_BLOCK_BYTES, target.largest_write);
    CHECK(!target.unaligned_block);
}

static void the_oldest_plays_are_left_out_of_a_file_that_is_too_small(void)
{
    static char wanted[] = HEADER JOGA_SKIPPED ROADS HUNTER;
    uint32_t kept = sizeof HEADER JOGA_SKIPPED ROADS - 1;
    uint32_t size = sizeof wanted - 2;
    memset(wanted + kept, ' ', size - kept);
    wanted[kept] = '#';
    wanted[size - 1] = '\n';
    wanted[size] = '\0';
    reset();
    add_three_plays();
    CHECK_EQUAL(STORE_DONE, export_to(size));
    check_text(wanted);
    CHECK_EQUAL(2, job.exported);
    CHECK_EQUAL(1, job.dropped);
}

static void a_play_of_a_track_the_lookup_does_not_know_is_left_out(void)
{
    static const char WANTED[] = HEADER HUNTER ROADS;
    reset();
    add(listening_fixture_dated(0, MONDAY, 0));
    ListeningPlay stranger = listening_fixture_dated(1, MONDAY, 300);
    stranger.track.key = 0x7777;
    add(stranger);
    add(listening_fixture_dated(3, MONDAY + 1, 0));
    CHECK_EQUAL(STORE_DONE, export_to(sizeof WANTED - 1));
    check_text(WANTED);
    CHECK_EQUAL(2, job.exported);
    CHECK_EQUAL(1, job.unknown);
}

static void a_damaged_record_is_left_out_of_the_export(void)
{
    static const char WANTED[] = HEADER HUNTER ROADS;
    reset();
    add_three_plays();
    disk.bytes[LISTENING_RING_OFFSET + 2 * LISTENING_RECORD_BYTES + 5] ^= 4;
    CHECK_EQUAL(STORE_DONE, export_to(sizeof WANTED - 1));
    check_text(WANTED);
    CHECK_EQUAL(1, job.cursor.damaged);
}

static bool long_text(void *context, const ListeningTrack *track, ListeningText *text)
{
    (void)context;
    (void)track;
    memcpy(text->artist, long_artist, sizeof long_artist);
    memset(text->album, 'b', sizeof text->album);
    strcpy(text->title, "t");
    return true;
}

static void a_tag_that_fills_its_buffer_is_cut_at_a_whole_character(void)
{
    reset();
    add(listening_fixture_play(0, 254000));
    memset(long_artist, 'a', sizeof long_artist - 1);
    memcpy(long_artist + sizeof long_artist - 3, "\xC3\xB6", 2);
    long_artist[sizeof long_artist - 1] = '\0';
    uint32_t bytes = measure((ListeningLookup){.text = long_text});
    CHECK_EQUAL(sizeof HEADER - 1 + 128 + 128 + 2 + 1 + 4 + 2 + 2 + 1, bytes);
    memset(long_artist, 'a', sizeof long_artist - 1);
    long_artist[sizeof long_artist - 2] = '\xC3';
    CHECK_EQUAL(bytes - 1, measure((ListeningLookup){.text = long_text}));
    CHECK(listening_export_write(&job, listening_disk_file(&target, bytes - 1)));
    CHECK_EQUAL(STORE_DONE, run());
    const uint8_t *line = target.bytes + sizeof HEADER - 1;
    CHECK_EQUAL('a', line[125]);
    CHECK_EQUAL('\t', line[126]);
    CHECK_EQUAL('b', line[127 + 126]);
    CHECK_EQUAL('\t', line[127 + 127]);
}

static void every_step_looks_up_at_most_one_play(void)
{
    reset();
    add_three_plays();
    listening_export_begin(&job, &log_file, listening_fixture_lookup());
    int steps = 0;
    while (listening_export_step(&job) == STORE_WORKING && steps < STEP_LIMIT) {
        CHECK_EQUAL(++steps, job.cursor.position);
    }
    CHECK_EQUAL(2, steps);
}

static void a_failed_write_fails_the_export(void)
{
    reset();
    for (int index = 0; index < 40; index++) {
        add(listening_fixture_dated(0, MONDAY, 0));
    }
    target.cut_write = 2;
    CHECK_EQUAL(STORE_FAILED, export_to(5 * STORE_BLOCK_BYTES));
    CHECK_EQUAL(2, target.writes);
    CHECK_EQUAL(STORE_FAILED, listening_export_step(&job));
}

static void an_unreadable_log_fails_the_export(void)
{
    reset();
    add_three_plays();
    listening_export_begin(&job, &log_file, listening_fixture_lookup());
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, run());
    CHECK(!listening_export_write(&job, listening_disk_file(&target, TEXT_CAPACITY)));
    disk.refuse_reads = false;
}

static void an_empty_log_exports_the_header_alone(void)
{
    reset();
    CHECK_EQUAL(STORE_DONE, export_to(sizeof HEADER - 1));
    check_text(HEADER);
    CHECK_EQUAL(0, job.exported);
}

static void writing_needs_a_finished_measure_and_a_file_that_holds_the_header(void)
{
    reset();
    add_three_plays();
    listening_export_begin(&job, &log_file, listening_fixture_lookup());
    CHECK(!listening_export_write(&job, listening_disk_file(&target, TEXT_CAPACITY)));
    CHECK_EQUAL(STORE_DONE, run());
    CHECK(!listening_export_write(&job, listening_disk_file(&target, sizeof HEADER - 2)));
    StoreFile read_only = listening_disk_file(&target, TEXT_CAPACITY);
    read_only.write = NULL;
    CHECK(!listening_export_write(&job, read_only));
    CHECK(listening_export_write(&job, listening_disk_file(&target, TEXT_CAPACITY)));
    CHECK(!listening_export_write(&job, listening_disk_file(&target, TEXT_CAPACITY)));
    listening_export_begin(&job, &log_file, (ListeningLookup){0});
    CHECK_EQUAL(STORE_FAILED, listening_export_step(&job));
    CHECK_EQUAL(0, target.writes);
}

static void the_log_is_not_written_by_an_export(void)
{
    reset();
    add_three_plays();
    int writes = disk.writes;
    CHECK_EQUAL(STORE_DONE, export_to(TEXT_CAPACITY));
    CHECK_EQUAL(writes, disk.writes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_log_is_exported_as_a_scrobbler_file_byte_for_byte),
        TEST_CASE(without_a_clock_the_timestamp_is_zero),
        TEST_CASE(tabs_and_line_breaks_in_a_tag_become_spaces_and_missing_fields_stay_empty),
        TEST_CASE(the_rest_of_a_larger_file_is_comment_lines),
        TEST_CASE(a_file_of_several_sectors_is_written_whole_sectors_first),
        TEST_CASE(the_oldest_plays_are_left_out_of_a_file_that_is_too_small),
        TEST_CASE(a_play_of_a_track_the_lookup_does_not_know_is_left_out),
        TEST_CASE(a_damaged_record_is_left_out_of_the_export),
        TEST_CASE(a_tag_that_fills_its_buffer_is_cut_at_a_whole_character),
        TEST_CASE(every_step_looks_up_at_most_one_play),
        TEST_CASE(a_failed_write_fails_the_export),
        TEST_CASE(an_unreadable_log_fails_the_export),
        TEST_CASE(an_empty_log_exports_the_header_alone),
        TEST_CASE(writing_needs_a_finished_measure_and_a_file_that_holds_the_header),
        TEST_CASE(the_log_is_not_written_by_an_export),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
