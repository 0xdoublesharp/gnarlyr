#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "playback/mix_resolve.h"

static uint8_t disk[2 * 1024 * 1024];
static uint16_t arena[393216 / 2];
static FixtureLibrary fixture;
static MixIdentity identities[3];

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

static bool read_identity(void *context, uint16_t index, MixIdentity *identity)
{
    const MixIdentity *source = context;
    *identity = source[index];
    return true;
}

static void save_source(MixTape *tape, MixSource source)
{
    memset(disk, 0, sizeof disk);
    MixMeasure measured;
    mix_measure_begin(&measured, source);
    while (mix_measure_step(&measured) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, measured.result);
    StoreFile io = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    MixTapeWrite writer;
    CHECK(mix_tape_write_begin(&writer, io, &measured, 5));
    while (mix_tape_write_step(&writer) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, writer.write.result);
    MixTapeCheck check;
    mix_tape_check_begin(&check, tape, io, 5);
    while (mix_tape_check_step(&check) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, check.result);
}

static void save_tape(MixTape *tape)
{
    save_source(tape, (MixSource){read_identity, identities, 3});
}

static void a_rescan_with_reordered_rows_keeps_the_same_songs_and_duplicates(void)
{
    Library *library = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 0, &identities[0]));
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 2, &identities[1]));
    identities[2] = identities[0];
    MixTape tape;
    save_tape(&tape);
    LibrarySectionInfo tracks = library->sections[LIBRARY_TRACKS];
    uint8_t temporary[64];
    CHECK(tracks.record_size <= sizeof temporary);
    memcpy(temporary, fixture.bytes + tracks.offset, tracks.record_size);
    memcpy(fixture.bytes + tracks.offset, fixture.bytes + tracks.offset + tracks.record_size,
           tracks.record_size);
    memcpy(fixture.bytes + tracks.offset + tracks.record_size, temporary, tracks.record_size);
    MixResolve job;
    CHECK(mix_resolve_begin(&job, library, &tape, (MixMemory){arena, sizeof arena}));
    for (int step = 0; step < 100 && job.result == STORE_WORKING; step++) {
        mix_resolve_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(0, job.missing);
    CHECK_EQUAL(3, job.count);
    TrackOrderSource source = mix_resolve_source(&job);
    uint16_t resolved[3] = {0};
    CHECK(source.read != NULL);
    if (source.read != NULL) {
        CHECK(source.read(source.context, 0, 3, resolved));
        CHECK_EQUAL(1, resolved[0]);
        CHECK_EQUAL(2, resolved[1]);
        CHECK_EQUAL(1, resolved[2]);
    }
}

static void missing_songs_and_changed_cue_boundaries_are_not_matched_to_other_tracks(void)
{
    Library *library = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 0, &identities[0]));
    identities[1] = (MixIdentity){.path = "Missing.flac"};
    identities[2] = identities[0];
    identities[2].start_ms = 1000;
    MixTape tape;
    save_tape(&tape);
    MixResolve job;
    CHECK(mix_resolve_begin(&job, library, &tape, (MixMemory){arena, sizeof arena}));
    while (mix_resolve_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(1, job.count);
    CHECK_EQUAL(2, job.missing);
    CHECK_EQUAL(0, job.ambiguous);
}

static void a_deleted_or_hidden_song_is_left_off_a_loaded_mixtape(void)
{
    Library *library = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 0, &identities[0]));
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 2, &identities[1]));
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 3, &identities[2]));
    MixTape tape;
    save_tape(&tape);
    static LibraryHidden hidden = {.count = 1, .tracks = {2}, .titles = {0xFFFF}};
    library->hidden = &hidden;
    MixResolve job;
    CHECK(mix_resolve_begin(&job, library, &tape, (MixMemory){arena, sizeof arena}));
    while (mix_resolve_step(&job) == STORE_WORKING) {
    }
    library->hidden = NULL;
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(2, job.count);
    CHECK_EQUAL(1, job.missing);
    uint16_t resolved[2] = {0};
    TrackOrderSource source = mix_resolve_source(&job);
    CHECK(source.read != NULL && source.read(source.context, 0, 2, resolved));
    CHECK_EQUAL(0, resolved[0]);
    CHECK_EQUAL(3, resolved[1]);
}

static void duplicate_library_identities_are_reported_as_ambiguous(void)
{
    Library *library = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 0, &identities[0]));
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 2, &identities[1]));
    identities[2] = identities[0];
    MixTape tape;
    save_tape(&tape);
    LibrarySectionInfo tracks = library->sections[LIBRARY_TRACKS];
    memcpy(fixture.bytes + tracks.offset + tracks.record_size, fixture.bytes + tracks.offset,
           tracks.record_size);
    MixResolve job;
    CHECK(mix_resolve_begin(&job, library, &tape, (MixMemory){arena, sizeof arena}));
    while (mix_resolve_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(1, job.count);
    CHECK_EQUAL(2, job.missing);
    CHECK_EQUAL(2, job.ambiguous);
    CHECK_EQUAL(2, job.tracks[0]);
}

static bool repeated_library(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (offset == 0x10000000u && length == sizeof "repeat.flac") {
        memcpy(bytes, "repeat.flac", length);
        return true;
    }
    if (length == 32 && offset % 32 == 0 && offset / 32 < UINT16_MAX) {
        memset(bytes, 0, length);
        return true;
    }
    return false;
}

static bool repeated_song(void *context, uint16_t index, MixIdentity *identity)
{
    (void)context;
    (void)index;
    *identity = (MixIdentity){.path = "repeat.flac"};
    return true;
}

static void maximum_capacity_fits_the_decoder_scratch_and_duplicate_rows_stay_bounded(void)
{
    Library library = {.source = {repeated_library, NULL}};
    library.sections[LIBRARY_TRACKS] = (LibrarySectionInfo){.count = UINT16_MAX, .record_size = 32};
    library.sections[LIBRARY_STRINGS] =
        (LibrarySectionInfo){.offset = 0x10000000u, .count = sizeof "repeat.flac"};
    MixTape tape;
    save_source(&tape, (MixSource){repeated_song, NULL, UINT16_MAX});
    MixResolve job;
    CHECK(!mix_resolve_begin(&job, &library, &tape, (MixMemory){arena, sizeof arena - 3}));
    arena[sizeof arena / sizeof arena[0] - 1] = 0xcafe;
    CHECK(mix_resolve_begin(&job, &library, &tape, (MixMemory){arena, sizeof arena}));
    int steps = 0;
    while (steps++ < 50000 && mix_resolve_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(UINT16_MAX, job.missing);
    CHECK_EQUAL(UINT16_MAX, job.ambiguous);
    CHECK_EQUAL(0, job.count);
    CHECK_EQUAL(0xcafe, arena[sizeof arena / sizeof arena[0] - 1]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_rescan_with_reordered_rows_keeps_the_same_songs_and_duplicates),
        TEST_CASE(missing_songs_and_changed_cue_boundaries_are_not_matched_to_other_tracks),
        TEST_CASE(a_deleted_or_hidden_song_is_left_off_a_loaded_mixtape),
        TEST_CASE(duplicate_library_identities_are_reported_as_ambiguous),
        TEST_CASE(maximum_capacity_fits_the_decoder_scratch_and_duplicate_rows_stay_bounded)};
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
