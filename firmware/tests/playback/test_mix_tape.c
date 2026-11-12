#include <string.h>

#include "check.h"
#include "core/bytes.h"
#include "core/hash.h"
#include "fixture_library.h"
#include "playback/mix_tape.h"

enum {
    CAPACITY = STORE_BLOCK_BYTES + UINT16_MAX * MIX_RECORD_MAX,
    COUNT_FIELD = 6,
    HASH_FIELD = 16,
    CHECK_FIELD = 24
};
static uint8_t disk[CAPACITY];
static int payload_reads;
static uint32_t largest_read;
static bool refuse_reads;
static MixIdentity songs[] = {
    {.path = "Björk/Homogenic/01 - Hunter.flac"},
    {.path = "Live/Concert.flac", .start_ms = 1000, .end_ms = 2000},
    {.path = "Live/Concert.flac", .start_ms = 2000},
    {.path = "Björk/Homogenic/01 - Hunter.flac"},
};

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (refuse_reads || offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    payload_reads += offset >= STORE_BLOCK_BYTES ? 1 : 0;
    largest_read = length > largest_read ? length : largest_read;
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    if (offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    memcpy(disk + offset, bytes, length);
    return true;
}

static bool read_song(void *context, uint16_t index, MixIdentity *identity)
{
    const MixIdentity *source = context;
    *identity = source[index];
    return true;
}

static void saved_paths_cue_boundaries_and_duplicates_survive_reopening(void)
{
    refuse_reads = false;
    memset(disk, 0, sizeof disk);
    MixSource source = {read_song, songs, 4};
    MixMeasure measured;
    mix_measure_begin(&measured, source);
    for (int step = 0; step < 20 && measured.result == STORE_WORKING; step++) {
        mix_measure_step(&measured);
    }
    CHECK_EQUAL(STORE_DONE, measured.result);
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite job = {0};
    CHECK(mix_tape_write_begin(&job, io, &measured, 77));
    for (int step = 0; step < 100 && job.write.result == STORE_WORKING; step++) {
        mix_tape_write_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.write.result);
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, io, 77);
    for (int step = 0; step < 100 && check.result == STORE_WORKING; step++) {
        mix_tape_check_step(&check);
    }
    CHECK_EQUAL(STORE_DONE, check.result);
    CHECK_EQUAL(STORE_READY, tape.load);
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &tape);
    for (int index = 0; index < 4; index++) {
        MixIdentity identity;
        CHECK_EQUAL(STORE_READY, mix_tape_next(&cursor, &identity));
        CHECK(strcmp(songs[index].path, identity.path) == 0);
        CHECK_EQUAL(songs[index].start_ms, identity.start_ms);
        CHECK_EQUAL(songs[index].end_ms, identity.end_ms);
    }
    MixIdentity identity;
    CHECK_EQUAL(STORE_EMPTY, mix_tape_next(&cursor, &identity));
}

static bool refuse_song(void *context, uint16_t index, MixIdentity *identity)
{
    (void)context;
    (void)index;
    (void)identity;
    return false;
}

static void invalid_measurements_and_files_fail_before_writing(void)
{
    MixMeasure measured;
    mix_measure_begin(&measured, (MixSource){.count = 1});
    CHECK_EQUAL(STORE_FAILED, mix_measure_step(&measured));
    mix_measure_begin(&measured, (MixSource){refuse_song, NULL, 1});
    CHECK_EQUAL(STORE_FAILED, mix_measure_step(&measured));
    CHECK_EQUAL(STORE_FAILED, mix_measure_step(&measured));

    mix_measure_begin(&measured, (MixSource){read_song, songs, 1});
    CHECK_EQUAL(STORE_DONE, mix_measure_step(&measured));
    StoreFile file = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite writer;
    MixMeasure invalid = measured;
    invalid.result = STORE_WORKING;
    CHECK(!mix_tape_write_begin(&writer, file, &invalid, 1));
    invalid = measured;
    invalid.index = 0;
    CHECK(!mix_tape_write_begin(&writer, file, &invalid, 1));
    invalid = measured;
    invalid.source.read = NULL;
    CHECK(!mix_tape_write_begin(&writer, file, &invalid, 1));
    invalid = measured;
    invalid.bytes = invalid.source.count * MIX_RECORD_MIN - 1;
    CHECK(!mix_tape_write_begin(&writer, file, &invalid, 1));
    invalid = measured;
    invalid.bytes = invalid.source.count * MIX_RECORD_MAX + 1;
    CHECK(!mix_tape_write_begin(&writer, file, &invalid, 1));
    file.size = STORE_BLOCK_BYTES - 1;
    CHECK(!mix_tape_write_begin(&writer, file, &measured, 1));
    file = (StoreFile){.source = {read_disk, NULL},
                       .write = write_disk,
                       .size = STORE_BLOCK_BYTES + measured.bytes - 1};
    CHECK(!mix_tape_write_begin(&writer, file, &measured, 1));
    file = (StoreFile){.write = write_disk, .size = sizeof disk};
    CHECK(!mix_tape_write_begin(&writer, file, &measured, 1));
    file = (StoreFile){.source = {read_disk, NULL}, .size = sizeof disk};
    CHECK(!mix_tape_write_begin(&writer, file, &measured, 1));
}

static void unusable_tapes_and_cursor_boundaries_fail_closed(void)
{
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, (StoreFile){0}, 1);
    CHECK_EQUAL(STORE_FAILED, mix_tape_check_step(&check));
    mix_tape_check_begin(&check, &tape,
                         (StoreFile){.source = {read_disk, NULL},
                                     .write = write_disk,
                                     .size = STORE_BLOCK_BYTES - 1},
                         1);
    CHECK_EQUAL(STORE_FAILED, mix_tape_check_step(&check));

    MixIdentity identity;
    MixTapeCursor cursor;
    tape = (MixTape){.load = STORE_CORRUPT};
    mix_tape_cursor(&cursor, &tape);
    CHECK_EQUAL(STORE_CORRUPT, mix_tape_next(&cursor, &identity));
    tape = (MixTape){.load = STORE_READY, .bytes = 1, .count = 0};
    mix_tape_cursor(&cursor, &tape);
    cursor.offset = 2;
    CHECK_EQUAL(STORE_CORRUPT, mix_tape_next(&cursor, &identity));
    cursor.offset = 0;
    CHECK_EQUAL(STORE_CORRUPT, mix_tape_next(&cursor, &identity));

    tape = (MixTape){.file = {.source = {read_disk, NULL}, .size = sizeof disk},
                     .load = STORE_READY,
                     .bytes = MIX_RECORD_MIN,
                     .count = 1};
    mix_tape_cursor(&cursor, &tape);
    refuse_reads = true;
    CHECK_EQUAL(STORE_UNREADABLE, mix_tape_next(&cursor, &identity));
    refuse_reads = false;
    memset(disk + STORE_BLOCK_BYTES, 0, MIX_RECORD_MIN);
    CHECK_EQUAL(STORE_CORRUPT, mix_tape_next(&cursor, &identity));
}

static void an_unterminated_library_path_is_rejected_instead_of_truncated(void)
{
    FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    LibraryTrack track;
    CHECK(library_read_track(library, 0, &track));
    library->sections[LIBRARY_STRINGS].count = track.path + 3;
    MixIdentity identity;
    CHECK_EQUAL(STORE_CORRUPT, mix_identity_read(library, 0, &identity));
}

static bool long_path(void *context, uint16_t index, MixIdentity *identity)
{
    (void)context;
    *identity = (MixIdentity){.start_ms = index, .end_ms = (uint32_t)index + 1000};
    memset(identity->path, 'x', MIX_PATH_BYTES - 1);
    identity->path[30] = '/';
    return true;
}

static void maximum_length_paths_and_a_full_mixtape_cross_sectors_without_truncation(void)
{
    MixMeasure measured;
    mix_measure_begin(&measured, (MixSource){long_path, NULL, UINT16_MAX});
    while (mix_measure_step(&measured) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, measured.result);
    CHECK_EQUAL((uint32_t)UINT16_MAX * MIX_RECORD_MAX, measured.bytes);
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite writer;
    CHECK(mix_tape_write_begin(&writer, io, &measured, 99));
    while (mix_tape_write_step(&writer) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, writer.write.result);
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, io, 99);
    while (mix_tape_check_step(&check) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, check.result);
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &tape);
    for (uint32_t index = 0; index < UINT16_MAX; index++) {
        MixIdentity identity;
        CHECK_EQUAL(STORE_READY, mix_tape_next(&cursor, &identity));
        CHECK_EQUAL(index, identity.start_ms);
        CHECK_EQUAL(MIX_PATH_BYTES - 1, strlen(identity.path));
    }
    MixIdentity identity;
    CHECK_EQUAL(STORE_EMPTY, mix_tape_next(&cursor, &identity));
    disk[STORE_BLOCK_BYTES + 513] ^= 1;
    mix_tape_check_begin(&check, &tape, io, 99);
    while (mix_tape_check_step(&check) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_FAILED, check.result);
    CHECK_EQUAL(STORE_CORRUPT, tape.load);
}

static void a_generation_mismatch_never_opens_an_orphaned_tape(void)
{
    saved_paths_cue_boundaries_and_duplicates_survive_reopening();
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, io, 78);
    CHECK_EQUAL(STORE_FAILED, mix_tape_check_step(&check));
    CHECK_EQUAL(STORE_CORRUPT, tape.load);
}

static StoreResult checked(MixTape *tape, uint32_t generation)
{
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeCheck check;
    mix_tape_check_begin(&check, tape, io, generation);
    while (mix_tape_check_step(&check) == STORE_WORKING) {
    }
    return check.result;
}

static void reseal(uint32_t payload_bytes)
{
    bytes_put_le32(disk + HASH_FIELD,
                   hash_update(HASH_START, disk + STORE_BLOCK_BYTES, payload_bytes));
    bytes_put_le32(disk + CHECK_FIELD, hash_update(HASH_START, disk, CHECK_FIELD));
}

static void checking_reads_each_payload_block_once_in_bounded_transfers(void)
{
    saved_paths_cue_boundaries_and_duplicates_survive_reopening();
    MixTape tape;
    payload_reads = 0;
    largest_read = 0;
    CHECK_EQUAL(STORE_DONE, checked(&tape, 77));
    CHECK_EQUAL((int)((tape.bytes + STORE_BLOCK_BYTES - 1) / STORE_BLOCK_BYTES), payload_reads);
    CHECK(largest_read <= STORE_BLOCK_BYTES);
}

static void records_that_do_not_parse_are_refused_even_with_a_matching_checksum(void)
{
    saved_paths_cue_boundaries_and_duplicates_survive_reopening();
    MixTape tape;
    CHECK_EQUAL(STORE_DONE, checked(&tape, 77));
    uint32_t bytes = tape.bytes;
    disk[STORE_BLOCK_BYTES + MIX_RECORD_FIELDS] = '/';
    reseal(bytes);
    CHECK_EQUAL(STORE_FAILED, checked(&tape, 77));
    CHECK_EQUAL(STORE_CORRUPT, tape.load);
    saved_paths_cue_boundaries_and_duplicates_survive_reopening();
    disk[STORE_BLOCK_BYTES]++;
    reseal(bytes);
    CHECK_EQUAL(STORE_FAILED, checked(&tape, 77));
    saved_paths_cue_boundaries_and_duplicates_survive_reopening();
    disk[COUNT_FIELD]++;
    reseal(bytes);
    CHECK_EQUAL(STORE_FAILED, checked(&tape, 77));
    CHECK_EQUAL(STORE_CORRUPT, tape.load);
    MixTapeCursor cursor;
    MixIdentity identity;
    mix_tape_cursor(&cursor, &tape);
    CHECK_EQUAL(STORE_CORRUPT, mix_tape_next(&cursor, &identity));
}

static void an_empty_mixtape_is_a_valid_tape(void)
{
    memset(disk, 0, sizeof disk);
    MixTape tape;
    CHECK_EQUAL(STORE_FAILED, checked(&tape, 3));
    CHECK_EQUAL(STORE_EMPTY, tape.load);
    MixMeasure measured;
    mix_measure_begin(&measured, (MixSource){0});
    CHECK_EQUAL(STORE_DONE, mix_measure_step(&measured));
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = STORE_BLOCK_BYTES};
    MixTapeWrite writer;
    CHECK(mix_tape_write_begin(&writer, io, &measured, 3));
    while (mix_tape_write_step(&writer) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, writer.write.result);
    CHECK_EQUAL(STORE_DONE, checked(&tape, 3));
    CHECK_EQUAL(0, tape.count);
    MixTapeCursor cursor;
    MixIdentity identity;
    mix_tape_cursor(&cursor, &tape);
    CHECK_EQUAL(STORE_EMPTY, mix_tape_next(&cursor, &identity));
}

static void a_source_that_changes_after_measuring_does_not_publish(void)
{
    memset(disk, 0, sizeof disk);
    MixMeasure measured;
    mix_measure_begin(&measured, (MixSource){read_song, songs, 3});
    CHECK_EQUAL(STORE_DONE, mix_measure_step(&measured));
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite writer;
    for (int change = -1; change <= 1; change += 2) {
        MixMeasure changed = measured;
        changed.source.count = (uint16_t)(3 + change);
        changed.index = changed.source.count;
        CHECK(mix_tape_write_begin(&writer, io, &changed, 4));
        while (mix_tape_write_step(&writer) == STORE_WORKING) {
        }
        CHECK_EQUAL(STORE_FAILED, writer.write.result);
        MixTape tape;
        CHECK_EQUAL(STORE_FAILED, checked(&tape, 4));
    }
    MixIdentity bad = {.path = "a//b.flac"};
    mix_measure_begin(&measured, (MixSource){read_song, &bad, 1});
    CHECK_EQUAL(STORE_FAILED, mix_measure_step(&measured));
    CHECK(!mix_tape_write_begin(&writer, io, &measured, 4));
}

static void each_slot_has_two_plainly_named_tape_files(void)
{
    char path[MIX_TAPE_PATH_BYTES];
    char other[MIX_TAPE_PATH_BYTES];
    CHECK(mix_tape_path(0, 0, path));
    CHECK(strcmp(path + strlen(path) - 11, "/mix00a.bin") == 0);
    CHECK(strncmp(path, MIX_CATALOG_PATH, strlen(path) - 10) == 0);
    CHECK(mix_tape_path(31, 1, path));
    CHECK(strcmp(path + strlen(path) - 11, "/mix31b.bin") == 0);
    CHECK(!mix_tape_path(MIX_CATALOG_SLOTS, 0, path));
    CHECK(!mix_tape_path(0, MIX_TAPE_BANKS, path));
    for (uint16_t slot = 1; slot < MIX_CATALOG_SLOTS; slot++) {
        CHECK(mix_tape_path(slot, 0, path));
        CHECK(mix_tape_path((uint16_t)(slot - 1), 0, other));
        CHECK(strcmp(path, other) != 0);
        CHECK(mix_tape_path(slot, 1, other));
        CHECK(strcmp(path, other) != 0);
    }
}

static void invalid_identities_are_never_encoded(void)
{
    static const char *const BAD[] = {"",       "/a.flac", "a/",   "a//b",      "./a.flac",
                                      "a/../b", "a:b",     "a\\b", "a\tb.flac", "a/.."};
    uint8_t bytes[MIX_RECORD_MAX];
    for (unsigned index = 0; index < sizeof BAD / sizeof BAD[0]; index++) {
        MixIdentity identity = {0};
        strcpy(identity.path, BAD[index]);
        CHECK_EQUAL(0, mix_identity_size(&identity));
        CHECK_EQUAL(0, mix_identity_encode(&identity, bytes));
        CHECK_EQUAL(0, mix_identity_hash(&identity));
    }
    MixIdentity backwards = {.path = "a.flac", .start_ms = 9, .end_ms = 9};
    CHECK_EQUAL(0, mix_identity_size(&backwards));
    MixIdentity dotted = {.path = ".hidden/..a/b.flac", .start_ms = 9, .end_ms = 10};
    MixIdentity decoded;
    uint16_t size = mix_identity_encode(&dotted, bytes);
    CHECK_EQUAL(MIX_RECORD_FIELDS + 18, size);
    CHECK_EQUAL(size, mix_identity_decode(bytes, size, &decoded));
    CHECK(mix_identity_equal(&dotted, &decoded));
    CHECK_EQUAL(mix_identity_hash(&dotted), hash_update(HASH_START, bytes, size));
    CHECK_EQUAL(0, mix_identity_decode(bytes, (uint32_t)size - 1, &decoded));
    decoded.end_ms++;
    CHECK(!mix_identity_equal(&dotted, &decoded));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(saved_paths_cue_boundaries_and_duplicates_survive_reopening),
        TEST_CASE(invalid_measurements_and_files_fail_before_writing),
        TEST_CASE(unusable_tapes_and_cursor_boundaries_fail_closed),
        TEST_CASE(an_unterminated_library_path_is_rejected_instead_of_truncated),
        TEST_CASE(maximum_length_paths_and_a_full_mixtape_cross_sectors_without_truncation),
        TEST_CASE(a_generation_mismatch_never_opens_an_orphaned_tape),
        TEST_CASE(checking_reads_each_payload_block_once_in_bounded_transfers),
        TEST_CASE(records_that_do_not_parse_are_refused_even_with_a_matching_checksum),
        TEST_CASE(an_empty_mixtape_is_a_valid_tape),
        TEST_CASE(a_source_that_changes_after_measuring_does_not_publish),
        TEST_CASE(each_slot_has_two_plainly_named_tape_files),
        TEST_CASE(invalid_identities_are_never_encoded),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
