#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "playback/mix_tape.h"

enum {
    CAPACITY = MIX_HEADER_BYTES + UINT16_MAX * MIX_RECORD_MAX
};
static uint8_t disk[CAPACITY];
static const MixIdentity songs[] = {
    {.path = "Björk/Homogenic/01 - Hunter.flac"},
    {.path = "Live/Concert.flac", .start_ms = 1000, .end_ms = 2000},
    {.path = "Live/Concert.flac", .start_ms = 2000},
    {.path = "Björk/Homogenic/01 - Hunter.flac"},
};

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (offset > sizeof disk || length > sizeof disk - offset) {
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
    memcpy(disk + offset, bytes, length);
    return true;
}

static bool read_song(const void *context, uint16_t index, MixIdentity *identity)
{
    const MixIdentity *source = context;
    *identity = source[index];
    return true;
}

static void saved_paths_cue_boundaries_and_duplicates_survive_reopening(void)
{
    memset(disk, 0, sizeof disk);
    MixSource source = {read_song, songs, 4};
    MixMeasure measured;
    mix_measure_begin(&measured, source);
    for (int step = 0; step < 20 && measured.result == MIX_WORKING; step++) {
        mix_measure_step(&measured);
    }
    CHECK_EQUAL(MIX_DONE, measured.result);
    MixIO io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite job = {0};
    CHECK(mix_tape_write_begin(&job, io, &measured, 77));
    for (int step = 0; step < 100 && job.result == MIX_WORKING; step++) {
        mix_tape_write_step(&job);
    }
    CHECK_EQUAL(MIX_DONE, job.result);
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, io, 77);
    for (int step = 0; step < 100 && check.result == MIX_WORKING; step++) {
        mix_tape_check_step(&check);
    }
    CHECK_EQUAL(MIX_DONE, check.result);
    CHECK_EQUAL(MIX_READY, tape.load);
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &tape);
    for (int index = 0; index < 4; index++) {
        MixIdentity identity;
        CHECK_EQUAL(MIX_READY, mix_tape_next(&cursor, &identity));
        CHECK(strcmp(songs[index].path, identity.path) == 0);
        CHECK_EQUAL(songs[index].start_ms, identity.start_ms);
        CHECK_EQUAL(songs[index].end_ms, identity.end_ms);
    }
    MixIdentity identity;
    CHECK_EQUAL(MIX_EMPTY, mix_tape_next(&cursor, &identity));
}

static void an_unterminated_library_path_is_rejected_instead_of_truncated(void)
{
    FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    LibraryTrack track;
    CHECK(library_read_track(library, 0, &track));
    library->sections[LIBRARY_STRINGS].count = track.path + 3;
    MixIdentity identity;
    CHECK_EQUAL(MIX_CORRUPT, mix_identity_read(library, 0, &identity));
}

static bool long_path(const void *context, uint16_t index, MixIdentity *identity)
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
    while (mix_measure_step(&measured) == MIX_WORKING) {
    }
    CHECK_EQUAL(MIX_DONE, measured.result);
    CHECK_EQUAL((uint32_t)UINT16_MAX * MIX_RECORD_MAX, measured.bytes);
    MixIO io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite writer;
    CHECK(mix_tape_write_begin(&writer, io, &measured, 99));
    while (mix_tape_write_step(&writer) == MIX_WORKING) {
    }
    CHECK_EQUAL(MIX_DONE, writer.result);
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, io, 99);
    while (mix_tape_check_step(&check) == MIX_WORKING) {
    }
    CHECK_EQUAL(MIX_DONE, check.result);
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &tape);
    for (uint32_t index = 0; index < UINT16_MAX; index++) {
        MixIdentity identity;
        CHECK_EQUAL(MIX_READY, mix_tape_next(&cursor, &identity));
        CHECK_EQUAL(index, identity.start_ms);
        CHECK_EQUAL(MIX_PATH_BYTES - 1, strlen(identity.path));
    }
    MixIdentity identity;
    CHECK_EQUAL(MIX_EMPTY, mix_tape_next(&cursor, &identity));
    disk[MIX_HEADER_BYTES + 513] ^= 1;
    mix_tape_check_begin(&check, &tape, io, 99);
    while (mix_tape_check_step(&check) == MIX_WORKING) {
    }
    CHECK_EQUAL(MIX_FAILED, check.result);
    CHECK_EQUAL(MIX_CORRUPT, tape.load);
}

static void a_generation_mismatch_never_opens_an_orphaned_tape(void)
{
    saved_paths_cue_boundaries_and_duplicates_survive_reopening();
    MixIO io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTape tape;
    MixTapeCheck check;
    mix_tape_check_begin(&check, &tape, io, 78);
    CHECK_EQUAL(MIX_FAILED, mix_tape_check_step(&check));
    CHECK_EQUAL(MIX_CORRUPT, tape.load);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(saved_paths_cue_boundaries_and_duplicates_survive_reopening),
        TEST_CASE(an_unterminated_library_path_is_rejected_instead_of_truncated),
        TEST_CASE(maximum_length_paths_and_a_full_mixtape_cross_sectors_without_truncation),
        TEST_CASE(a_generation_mismatch_never_opens_an_orphaned_tape)};
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
