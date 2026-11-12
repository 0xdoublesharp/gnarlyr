#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "listening_fixture.h"

enum {
    VERSION_FIELD = 4,
    CHECK_FIELD = 24,
    RECORD_CHECK_FIELD = 60,
    SLOT_ONE = LISTENING_RING_OFFSET + LISTENING_RECORD_BYTES,
    CLEARED_BYTES = 4096
};

static ListeningDisk disk;
static ListeningDisk before;
static ListeningLog log_file;

static StoreLoad reopen(void)
{
    return listening_log_open(&log_file, listening_disk_config(&disk));
}

static void reset(void)
{
    listening_disk_reset(&disk);
    CHECK_EQUAL(STORE_EMPTY, reopen());
}

static void append_plays(uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        ListeningPlay play = listening_fixture_play((uint8_t)(index % 5), 1000 + index);
        CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    }
}

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void a_play_is_read_back_with_every_field(void)
{
    reset();
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_BOOT));
    ListeningPlay play = {
        .track = {.key = 0x11223344, .album = 0x55667788, .artist = 0x99aabbcc, .index = 0xddee},
        .length_ms = 240000,
        .listened_ms = 180000,
        .start_ms = 30000,
        .end_ms = 210000,
        .uptime_s = 4321,
        .clock_s = 1794268800,
        .offset_minutes = -480,
        .end = LISTENING_REPLACED,
        .output = LISTENING_OUTPUT_USB_DAC,
        .format = LISTENING_FORMAT_HIRES};
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_READY, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(1, seen.first.sequence);
    CHECK_EQUAL(1, seen.first.boot);
    CHECK_EQUAL(0x11223344, seen.first.play.track.key);
    CHECK_EQUAL(0x55667788, seen.first.play.track.album);
    CHECK_EQUAL(0x99aabbcc, seen.first.play.track.artist);
    CHECK_EQUAL(0xddee, seen.first.play.track.index);
    CHECK_EQUAL(240000, seen.first.play.length_ms);
    CHECK_EQUAL(180000, seen.first.play.listened_ms);
    CHECK_EQUAL(30000, seen.first.play.start_ms);
    CHECK_EQUAL(210000, seen.first.play.end_ms);
    CHECK_EQUAL(4321, seen.first.play.uptime_s);
    CHECK_EQUAL(1794268800, seen.first.play.clock_s);
    CHECK_EQUAL(-480, seen.first.play.offset_minutes);
    CHECK_EQUAL(LISTENING_REPLACED, seen.first.play.end);
    CHECK_EQUAL(LISTENING_OUTPUT_USB_DAC, seen.first.play.output);
    CHECK_EQUAL(LISTENING_FORMAT_HIRES, seen.first.play.format);
}

static void a_play_costs_two_writes_of_at_most_one_sector(void)
{
    reset();
    append_plays(3);
    int writes = disk.writes;
    disk.largest_write = 0;
    append_plays(1);
    CHECK_EQUAL(2, disk.writes - writes);
    CHECK_EQUAL(STORE_BLOCK_BYTES, disk.largest_write);
    CHECK(!disk.unaligned_block);
}

static void the_file_is_two_counter_blocks_and_a_ring_of_64_byte_records(void)
{
    reset();
    ListeningPlay play = listening_fixture_play(0, 254000);
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK(memcmp(disk.bytes, "DSLL", 4) == 0);
    CHECK_EQUAL(1, disk.bytes[VERSION_FIELD]);
    CHECK_EQUAL(0, disk.bytes[STORE_BLOCK_BYTES]);
    const uint8_t *record = disk.bytes + SLOT_ONE;
    CHECK_EQUAL(1, get32(record));
    CHECK_EQUAL(0x1001, get32(record + 4));
    CHECK_EQUAL(0xA1, get32(record + 8));
    CHECK_EQUAL(0xB1, get32(record + 12));
    CHECK_EQUAL(254000, get32(record + 16));
    CHECK_EQUAL(254000, get32(record + 20));
    CHECK_EQUAL(10, record[44]);
    CHECK_EQUAL(LISTENING_FINISHED, record[46]);
    CHECK_EQUAL(LISTENING_FORMAT_LOSSLESS, record[48]);
    CHECK_EQUAL(0, record[49]);
    CHECK_EQUAL(hash_update(HASH_START, record, RECORD_CHECK_FIELD),
                get32(record + RECORD_CHECK_FIELD));
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK(memcmp(disk.bytes + STORE_BLOCK_BYTES, "DSLL", 4) == 0);
    CHECK_EQUAL(2, get32(record + LISTENING_RECORD_BYTES));
    CHECK_EQUAL(263168, LISTENING_LOG_FILE_BYTES);
    CHECK(memcmp(LISTENING_LOG_PATH + strlen(LISTENING_LOG_PATH) - 12, "/listens.bin", 12) == 0);
}

static void counters_add_up_time_plays_skips_outputs_and_formats(void)
{
    reset();
    ListeningPlay play = listening_fixture_play(0, 254000);
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    play = listening_fixture_play(1, 5000);
    play.output = LISTENING_OUTPUT_BALANCED;
    play.format = LISTENING_FORMAT_HIRES;
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    play = listening_fixture_play(2, 60000);
    play.end = LISTENING_STOPPED;
    play.output = LISTENING_OUTPUT_BLUETOOTH;
    play.format = LISTENING_FORMAT_LOSSY;
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_READY, reopen());
    const ListeningCounters *counters = &log_file.root.counters;
    CHECK_EQUAL(319000, counters->totals.listened_ms);
    CHECK_EQUAL(3, counters->totals.plays);
    CHECK_EQUAL(1, counters->totals.skips);
    CHECK_EQUAL(254000, counters->output_ms[LISTENING_OUTPUT_SINGLE_ENDED]);
    CHECK_EQUAL(5000, counters->output_ms[LISTENING_OUTPUT_BALANCED]);
    CHECK_EQUAL(60000, counters->output_ms[LISTENING_OUTPUT_BLUETOOTH]);
    CHECK_EQUAL(0, counters->output_ms[LISTENING_OUTPUT_USB_DAC]);
    CHECK_EQUAL(60000, counters->format_ms[LISTENING_FORMAT_LOSSY]);
    CHECK_EQUAL(254000, counters->format_ms[LISTENING_FORMAT_LOSSLESS]);
    CHECK_EQUAL(5000, counters->format_ms[LISTENING_FORMAT_HIRES]);
    CHECK_EQUAL(333, listening_skip_permille(&counters->totals));
    CHECK_EQUAL(0, log_file.session.plays);
}

static void time_beyond_49_days_is_kept_in_the_counters(void)
{
    reset();
    ListeningPlay play = listening_fixture_play(0, UINT32_MAX);
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(2 * (long long)UINT32_MAX, log_file.root.counters.totals.listened_ms);
}

static void boots_and_charging_are_counted_with_one_write_each(void)
{
    reset();
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_BOOT));
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_BOOT));
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_CHARGE_STARTED));
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_CHARGE_STARTED));
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_CHARGE_STARTED));
    CHECK_EQUAL(STORE_DONE, listening_log_note(&log_file, LISTENING_CHARGE_FULL));
    CHECK_EQUAL(6, disk.writes);
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(2, log_file.root.counters.boots);
    CHECK_EQUAL(3, log_file.root.counters.charge_sessions);
    CHECK_EQUAL(1, log_file.root.counters.full_charges);
    CHECK_EQUAL(0, log_file.root.kept);
}

static void the_oldest_plays_are_overwritten_when_the_ring_is_full(void)
{
    reset();
    append_plays(LISTENING_LOG_CAPACITY + 10);
    CHECK_EQUAL(STORE_READY, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(LISTENING_LOG_CAPACITY, seen.count);
    CHECK_EQUAL(11, seen.first.sequence);
    CHECK_EQUAL(1010, seen.first.play.listened_ms);
    CHECK_EQUAL(LISTENING_LOG_CAPACITY + 10, seen.last.sequence);
    CHECK(seen.ordered);
    CHECK_EQUAL(LISTENING_LOG_CAPACITY + 10,
                get32(disk.bytes + LISTENING_RING_OFFSET + 10 * LISTENING_RECORD_BYTES));
}

static void counters_survive_the_ring_wrapping(void)
{
    reset();
    uint32_t plays = LISTENING_LOG_CAPACITY + 10;
    append_plays(plays);
    CHECK_EQUAL(STORE_READY, reopen());
    ListeningSeen seen;
    listening_fixture_read_all(&log_file, &seen);
    uint64_t every = (uint64_t)plays * 1000 + (uint64_t)plays * (plays - 1) / 2;
    CHECK_EQUAL(plays, log_file.root.counters.totals.plays);
    CHECK_EQUAL(every, log_file.root.counters.totals.listened_ms);
    CHECK_EQUAL(every, log_file.root.counters.output_ms[LISTENING_OUTPUT_SINGLE_ENDED]);
    CHECK_EQUAL(every - 10 * 1000 - 45, seen.listened_ms);
}

static void a_torn_record_leaves_the_log_as_it_was(void)
{
    reset();
    append_plays(5);
    before = disk;
    for (uint32_t cut = 0; cut < LISTENING_RECORD_BYTES; cut++) {
        disk = before;
        CHECK_EQUAL(STORE_READY, reopen());
        disk.cut_write = disk.writes + 1;
        disk.cut_bytes = cut;
        ListeningPlay play = listening_fixture_play(3, 305000);
        CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
        disk.cut_write = 0;
        CHECK_EQUAL(STORE_READY, reopen());
        ListeningSeen seen;
        CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
        CHECK_EQUAL(5, seen.count);
        CHECK_EQUAL(5, log_file.root.counters.totals.plays);
        CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
        CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
        CHECK_EQUAL(6, seen.last.sequence);
        CHECK_EQUAL(305000, seen.last.play.listened_ms);
    }
}

static void a_torn_counters_write_keeps_the_totals_and_ignores_the_new_record(void)
{
    reset();
    append_plays(5);
    before = disk;
    for (uint32_t cut = 0; cut <= STORE_BLOCK_BYTES; cut++) {
        disk = before;
        CHECK_EQUAL(STORE_READY, reopen());
        disk.cut_write = disk.writes + 2;
        disk.cut_bytes = cut;
        ListeningPlay play = listening_fixture_play(3, 305000);
        StoreResult result = listening_log_append(&log_file, &play);
        uint32_t plays = result == STORE_DONE ? 6 : 5;
        CHECK_EQUAL(plays, log_file.root.counters.totals.plays);
        disk.cut_write = 0;
        CHECK_EQUAL(STORE_READY, reopen());
        ListeningSeen seen;
        CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
        CHECK_EQUAL(plays, seen.count);
        CHECK_EQUAL(plays, log_file.root.counters.totals.plays);
        CHECK_EQUAL(plays, seen.last.sequence);
    }
}

static void a_damaged_record_is_skipped_and_the_others_are_read(void)
{
    reset();
    append_plays(20);
    disk.bytes[LISTENING_RING_OFFSET + 9 * LISTENING_RECORD_BYTES + 21] ^= 0x10;
    CHECK_EQUAL(STORE_READY, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(1, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(19, seen.count);
    CHECK_EQUAL(1, seen.first.sequence);
    CHECK_EQUAL(20, seen.last.sequence);
    CHECK(seen.ordered);
    CHECK_EQUAL(20, log_file.root.counters.totals.plays);
    append_plays(1);
    CHECK_EQUAL(1, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(20, seen.count);
}

static void a_damaged_newest_record_is_skipped_too(void)
{
    reset();
    append_plays(20);
    memset(disk.bytes + LISTENING_RING_OFFSET + 20 * LISTENING_RECORD_BYTES, 0xff, 17);
    CHECK_EQUAL(STORE_READY, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(1, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(19, seen.count);
    CHECK_EQUAL(19, seen.last.sequence);
}

static void a_record_with_the_sequence_of_another_slot_is_not_read(void)
{
    reset();
    append_plays(20);
    uint8_t *ring = disk.bytes + LISTENING_RING_OFFSET;
    memcpy(ring + 7 * LISTENING_RECORD_BYTES, ring + 3 * LISTENING_RECORD_BYTES,
           LISTENING_RECORD_BYTES);
    ListeningSeen seen;
    CHECK_EQUAL(1, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(19, seen.count);
    CHECK(seen.ordered);
}

static void a_new_file_over_old_records_reads_as_an_empty_log(void)
{
    reset();
    append_plays(100);
    memset(disk.bytes, 0, CLEARED_BYTES);
    CHECK_EQUAL(STORE_EMPTY, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(0, seen.count);
    for (uint32_t index = 0; index < 60; index++) {
        ListeningPlay play = listening_fixture_play(4, 7);
        CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    }
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(60, seen.count);
    CHECK_EQUAL(60 * 7, seen.listened_ms);
    CHECK_EQUAL(60, log_file.root.counters.totals.plays);
}

static void nothing_is_written_while_the_log_is_switched_off(void)
{
    listening_disk_reset(&disk);
    ListeningLogConfig config = listening_disk_config(&disk);
    config.enabled = false;
    CHECK_EQUAL(STORE_EMPTY, listening_log_open(&log_file, config));
    ListeningPlay play = listening_fixture_play(0, 254000);
    CHECK_EQUAL(STORE_IDLE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_IDLE, listening_log_note(&log_file, LISTENING_BOOT));
    CHECK_EQUAL(STORE_IDLE, listening_log_note(&log_file, LISTENING_CHARGE_FULL));
    CHECK_EQUAL(0, disk.writes);
    CHECK_EQUAL(0, log_file.session.plays);
    CHECK_EQUAL(0, log_file.root.counters.totals.plays);
    listening_log_enable(&log_file, true);
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    listening_log_enable(&log_file, false);
    int writes = disk.writes;
    CHECK_EQUAL(STORE_IDLE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(writes, disk.writes);
    CHECK_EQUAL(1, log_file.session.plays);
}

static void this_session_counts_only_plays_since_the_log_was_opened(void)
{
    reset();
    append_plays(4);
    CHECK_EQUAL(4, log_file.session.plays);
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(0, log_file.session.plays);
    ListeningPlay play = listening_fixture_play(1, 2000);
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
    CHECK_EQUAL(1, log_file.session.plays);
    CHECK_EQUAL(2000, log_file.session.listened_ms);
    CHECK_EQUAL(1, log_file.session.skips);
    CHECK_EQUAL(5, log_file.root.counters.totals.plays);
}

static void an_unreadable_read_back_stops_writing_until_the_log_is_opened_again(void)
{
    reset();
    append_plays(2);
    ListeningPlay play = listening_fixture_play(0, 254000);
    disk.blind_from_write = disk.writes + 2;
    CHECK_EQUAL(STORE_UNCERTAIN, listening_log_append(&log_file, &play));
    CHECK(log_file.needs_reload);
    disk.blind_from_write = 0;
    int writes = disk.writes;
    CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_FAILED, listening_log_note(&log_file, LISTENING_BOOT));
    CHECK_EQUAL(writes, disk.writes);
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(3, log_file.root.counters.totals.plays);
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_file, &play));
}

static void seal(uint8_t *block)
{
    uint32_t check = hash_update(HASH_START, block, CHECK_FIELD);
    for (int index = 0; index < 4; index++) {
        block[CHECK_FIELD + index] = (uint8_t)(check >> (8 * index));
    }
}

static void a_later_version_is_never_overwritten(void)
{
    reset();
    append_plays(1);
    disk.bytes[VERSION_FIELD] = 3;
    seal(disk.bytes);
    before = disk;
    CHECK_EQUAL(STORE_UNSUPPORTED, reopen());
    ListeningPlay play = listening_fixture_play(0, 254000);
    CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
    CHECK_EQUAL(STORE_FAILED, listening_log_note(&log_file, LISTENING_BOOT));
    CHECK(memcmp(before.bytes, disk.bytes, sizeof disk.bytes) == 0);
}

static void seal_record(uint8_t *record)
{
    uint32_t check = hash_update(HASH_START, record, RECORD_CHECK_FIELD);
    for (int index = 0; index < 4; index++) {
        record[RECORD_CHECK_FIELD + index] = (uint8_t)(check >> (8 * index));
    }
}

static void a_log_of_another_version_is_unsupported(void)
{
    reset();
    append_plays(2);
    for (int bank = 0; bank < 2; bank++) {
        uint8_t *block = disk.bytes + bank * STORE_BLOCK_BYTES;
        CHECK(block[0] != 'D' || block[VERSION_FIELD] == 1);
        if (block[0] == 'D') {
            block[VERSION_FIELD] = 2;
            seal(block);
        }
    }
    CHECK_EQUAL(STORE_UNSUPPORTED, reopen());
}

static void a_record_with_unknown_clock_flags_is_damaged(void)
{
    reset();
    append_plays(2);
    uint8_t *record = disk.bytes + SLOT_ONE;
    record[49] = 0x02;
    seal_record(record);
    CHECK_EQUAL(STORE_READY, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(1, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(1, seen.count);
}

static void one_damaged_counter_block_falls_back_to_the_one_before(void)
{
    reset();
    append_plays(4);
    uint8_t newest = log_file.bank;
    disk.bytes[newest * STORE_BLOCK_BYTES + STORE_FIELD_BYTES + 16] ^= 1;
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(3, log_file.root.counters.totals.plays);
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(3, seen.count);
}

static void two_damaged_counter_blocks_start_the_log_again(void)
{
    reset();
    append_plays(4);
    disk.bytes[STORE_FIELD_BYTES + 16] ^= 1;
    disk.bytes[STORE_BLOCK_BYTES + STORE_FIELD_BYTES + 16] ^= 1;
    CHECK_EQUAL(STORE_CORRUPT, reopen());
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(0, seen.count);
    append_plays(2);
    CHECK_EQUAL(STORE_READY, reopen());
    CHECK_EQUAL(2, log_file.root.counters.totals.plays);
    CHECK_EQUAL(0, listening_fixture_read_all(&log_file, &seen));
    CHECK_EQUAL(2, seen.count);
}

static void a_file_that_is_too_short_or_cannot_be_read_is_refused(void)
{
    listening_disk_reset(&disk);
    ListeningLogConfig config = listening_disk_config(&disk);
    config.file.size = LISTENING_LOG_FILE_BYTES - 1;
    CHECK_EQUAL(STORE_UNREADABLE, listening_log_open(&log_file, config));
    ListeningPlay play = listening_fixture_play(0, 254000);
    CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_UNREADABLE, reopen());
    disk.refuse_reads = false;
    CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
    CHECK_EQUAL(0, disk.writes);
}

static void an_unreadable_sector_fails_a_pass(void)
{
    reset();
    append_plays(20);
    ListeningSeen seen = {0};
    ListeningCursor cursor;
    listening_cursor_begin(&cursor, &log_file, LISTENING_SECTOR_RECORDS);
    disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, listening_cursor_step(&cursor, (ListeningVisitor){NULL, &seen}));
    disk.refuse_reads = false;
    CHECK_EQUAL(0, cursor.position);
}

static void a_pass_reads_one_sector_at_a_time(void)
{
    reset();
    append_plays(20);
    disk.reads = 0;
    ListeningSeen seen;
    listening_fixture_read_all(&log_file, &seen);
    CHECK_EQUAL(20, seen.count);
    CHECK_EQUAL(3, disk.reads);
}

static void a_play_with_a_kind_outside_the_format_is_refused(void)
{
    reset();
    ListeningPlay play = listening_fixture_play(0, 254000);
    play.output = (ListeningOutput)LISTENING_OUTPUT_COUNT;
    CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
    play = listening_fixture_play(0, 254000);
    play.format = (ListeningFormat)LISTENING_FORMAT_COUNT;
    CHECK_EQUAL(STORE_FAILED, listening_log_append(&log_file, &play));
    CHECK_EQUAL(0, disk.writes);
}

static void a_play_counts_as_listened_from_half_its_length(void)
{
    ListeningPlay play = {.length_ms = 200001, .listened_ms = 100000, .end = LISTENING_FINISHED};
    CHECK(!listening_play_listened(&play));
    play.listened_ms = 100001;
    play.end = LISTENING_SKIPPED;
    CHECK(listening_play_listened(&play));
    play.length_ms = 0;
    CHECK(!listening_play_listened(&play));
    play.end = LISTENING_FINISHED;
    CHECK(listening_play_listened(&play));
}

static void lossless_above_48_khz_or_16_bits_is_high_resolution(void)
{
    CHECK_EQUAL(LISTENING_FORMAT_LOSSY, listening_format_class(false, 96000, 24));
    CHECK_EQUAL(LISTENING_FORMAT_LOSSLESS, listening_format_class(true, 44100, 16));
    CHECK_EQUAL(LISTENING_FORMAT_LOSSLESS, listening_format_class(true, 48000, 16));
    CHECK_EQUAL(LISTENING_FORMAT_HIRES, listening_format_class(true, 44100, 24));
    CHECK_EQUAL(LISTENING_FORMAT_HIRES, listening_format_class(true, 88200, 16));
}

static void the_skip_rate_is_skips_per_thousand_plays(void)
{
    ListeningTotals totals = {0};
    CHECK_EQUAL(0, listening_skip_permille(&totals));
    totals = (ListeningTotals){.plays = 8, .skips = 2};
    CHECK_EQUAL(250, listening_skip_permille(&totals));
    totals = (ListeningTotals){.plays = UINT32_MAX, .skips = UINT32_MAX};
    CHECK_EQUAL(1000, listening_skip_permille(&totals));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_play_is_read_back_with_every_field),
        TEST_CASE(a_play_costs_two_writes_of_at_most_one_sector),
        TEST_CASE(the_file_is_two_counter_blocks_and_a_ring_of_64_byte_records),
        TEST_CASE(counters_add_up_time_plays_skips_outputs_and_formats),
        TEST_CASE(time_beyond_49_days_is_kept_in_the_counters),
        TEST_CASE(boots_and_charging_are_counted_with_one_write_each),
        TEST_CASE(the_oldest_plays_are_overwritten_when_the_ring_is_full),
        TEST_CASE(counters_survive_the_ring_wrapping),
        TEST_CASE(a_torn_record_leaves_the_log_as_it_was),
        TEST_CASE(a_torn_counters_write_keeps_the_totals_and_ignores_the_new_record),
        TEST_CASE(a_damaged_record_is_skipped_and_the_others_are_read),
        TEST_CASE(a_damaged_newest_record_is_skipped_too),
        TEST_CASE(a_record_with_the_sequence_of_another_slot_is_not_read),
        TEST_CASE(a_new_file_over_old_records_reads_as_an_empty_log),
        TEST_CASE(nothing_is_written_while_the_log_is_switched_off),
        TEST_CASE(this_session_counts_only_plays_since_the_log_was_opened),
        TEST_CASE(an_unreadable_read_back_stops_writing_until_the_log_is_opened_again),
        TEST_CASE(a_later_version_is_never_overwritten),
        TEST_CASE(a_log_of_another_version_is_unsupported),
        TEST_CASE(a_record_with_unknown_clock_flags_is_damaged),
        TEST_CASE(one_damaged_counter_block_falls_back_to_the_one_before),
        TEST_CASE(two_damaged_counter_blocks_start_the_log_again),
        TEST_CASE(a_file_that_is_too_short_or_cannot_be_read_is_refused),
        TEST_CASE(an_unreadable_sector_fails_a_pass),
        TEST_CASE(a_pass_reads_one_sector_at_a_time),
        TEST_CASE(a_play_with_a_kind_outside_the_format_is_refused),
        TEST_CASE(a_play_counts_as_listened_from_half_its_length),
        TEST_CASE(lossless_above_48_khz_or_16_bits_is_high_resolution),
        TEST_CASE(the_skip_rate_is_skips_per_thousand_plays),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
