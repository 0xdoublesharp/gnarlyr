#include "check.h"
#include "fixture_library.h"
#include "playback/listening_track.h"
#include "playback/mix_identity.h"

static FixtureLibrary fixture;

static void a_track_gets_the_keys_the_library_and_mixtapes_use(void)
{
    Library *library = fixture_library_open(&fixture);
    for (uint16_t track = 0; track < FIXTURE_TRACKS; track++) {
        ListeningTrack identity;
        MixIdentity mix;
        LibraryTrack record;
        CHECK_EQUAL(STORE_READY, listening_track_read(library, track, &identity));
        CHECK_EQUAL(STORE_READY, mix_identity_read(library, track, &mix));
        CHECK(library_read_track(library, track, &record));
        CHECK_EQUAL(mix_identity_hash(&mix), identity.key);
        CHECK_EQUAL(library_album_key(library, record.album), identity.album);
        CHECK_EQUAL(track, identity.index);
        CHECK(identity.key != 0 && identity.album != 0 && identity.artist != 0);
    }
}

static void tracks_share_an_artist_key_exactly_when_they_share_the_artist(void)
{
    Library *library = fixture_library_open(&fixture);
    int shared = 0;
    for (uint16_t first = 0; first < FIXTURE_TRACKS; first++) {
        for (uint16_t second = 0; second < first; second++) {
            ListeningTrack one;
            ListeningTrack other;
            LibraryTrack records[2];
            CHECK_EQUAL(STORE_READY, listening_track_read(library, first, &one));
            CHECK_EQUAL(STORE_READY, listening_track_read(library, second, &other));
            CHECK(library_read_track(library, first, &records[0]));
            CHECK(library_read_track(library, second, &records[1]));
            CHECK_EQUAL(records[0].artist == records[1].artist, one.artist == other.artist);
            CHECK(one.key != other.key);
            shared += one.artist == other.artist ? 1 : 0;
        }
    }
    CHECK(shared > 0);
}

static void the_parts_of_one_file_have_different_keys(void)
{
    Library *library = fixture_cue_library_open(&fixture);
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    int parts = 0;
    for (uint16_t first = 0; first < count; first++) {
        for (uint16_t second = 0; second < first; second++) {
            ListeningTrack one;
            ListeningTrack other;
            LibraryTrack records[2];
            CHECK_EQUAL(STORE_READY, listening_track_read(library, first, &one));
            CHECK_EQUAL(STORE_READY, listening_track_read(library, second, &other));
            CHECK(library_read_track(library, first, &records[0]));
            CHECK(library_read_track(library, second, &records[1]));
            CHECK(one.key != other.key);
            parts += records[0].path == records[1].path ? 1 : 0;
        }
    }
    CHECK(parts > 0);
}

static void a_track_outside_the_library_has_no_identity(void)
{
    Library *library = fixture_library_open(&fixture);
    ListeningTrack identity = {.key = 5};
    CHECK_EQUAL(STORE_CORRUPT, listening_track_read(library, FIXTURE_TRACKS, &identity));
    CHECK_EQUAL(0, identity.key);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_track_gets_the_keys_the_library_and_mixtapes_use),
        TEST_CASE(tracks_share_an_artist_key_exactly_when_they_share_the_artist),
        TEST_CASE(the_parts_of_one_file_have_different_keys),
        TEST_CASE(a_track_outside_the_library_has_no_identity),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
