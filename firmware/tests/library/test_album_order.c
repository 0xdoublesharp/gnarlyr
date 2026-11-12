#include "check.h"
#include "fixture_library.h"
#include "library/album_order.h"

static FixtureLibrary fixture;
static AlbumOrder order;

static bool prepare(Library *library, LibrarySection section, uint16_t artist, uint32_t seed)
{
    BrowseQuery query = browse_root_query(BROWSE_GROUP_ALBUMS);
    query.group = section;
    query.subject = artist;
    if (!album_order_begin(&order, library, query, seed)) {
        return false;
    }
    while (!order.ready && !order.failed) {
        album_order_step(&order);
    }
    return order.ready && !order.failed;
}

static void every_album_plays_in_year_title_order_with_all_its_tracks(void)
{
    CHECK(prepare(fixture_library_open(&fixture), LIBRARY_ARTISTS, 3, 0));
    CHECK_EQUAL(4, order.count);
    uint16_t tracks[4] = {0};
    CHECK(album_order_read(&order, 0, 4, tracks));
    static const uint16_t EXPECTED[] = {5, 6, 2, 3};
    for (int index = 0; index < 4; index++) {
        CHECK_EQUAL(EXPECTED[index], tracks[index]);
    }
    CHECK(album_order_read(&order, 1, 1, tracks));
    CHECK_EQUAL(6, tracks[0]);
    CHECK(!album_order_read(&order, 4, 1, tracks));
}

static void album_artists_play_whole_albums(void)
{
    CHECK(prepare(fixture_library_open(&fixture), LIBRARY_ALBUM_ARTISTS, 3, 0));
    CHECK_EQUAL(2, order.count);
    uint16_t tracks[2] = {0};
    CHECK(album_order_read(&order, 0, 2, tracks));
    CHECK_EQUAL(2, tracks[0]);
    CHECK_EQUAL(3, tracks[1]);
}

static void shuffle_changes_album_order_and_preserves_track_order(void)
{
    bool reversed = false;
    for (uint32_t seed = 1; seed < 10; seed++) {
        CHECK(prepare(fixture_library_open(&fixture), LIBRARY_ARTISTS, 3, seed));
        uint16_t tracks[4] = {0};
        CHECK(album_order_read(&order, 0, 4, tracks));
        CHECK(tracks[0] == 5 || tracks[0] == 2);
        CHECK_EQUAL(tracks[0] + 1, tracks[1]);
        CHECK_EQUAL(tracks[2] + 1, tracks[3]);
        CHECK(tracks[0] != tracks[2]);
        reversed |= tracks[0] == 2;
    }
    CHECK(reversed);
}

static void cue_parts_keep_their_individual_track_indices(void)
{
    Library *library = fixture_cue_library_open(&fixture);
    CHECK(prepare(library, LIBRARY_ALBUM_ARTISTS, 1, 0));
    uint16_t tracks[8] = {0};
    CHECK(order.count <= 8);
    CHECK(album_order_read(&order, 0, (uint16_t)order.count, tracks));
    bool found = false;
    for (uint32_t index = 0; index + 2 < order.count; index++) {
        if (tracks[index] == 2) {
            CHECK_EQUAL(3, tracks[index + 1]);
            CHECK_EQUAL(4, tracks[index + 2]);
            found = true;
        }
    }
    CHECK(found);
}

static void the_queue_accepts_65535_tracks_and_refuses_overflow(void)
{
    Library *library = fixture_library_open(&fixture);
    library->sections[LIBRARY_TRACKS].count = UINT16_MAX;
    uint8_t *album = fixture.bytes + library->sections[LIBRARY_ALBUMS].offset + 3 * 16;
    album[10] = album[11] = 0;
    album[12] = album[13] = 255;
    BrowseQuery query = browse_root_query(BROWSE_GROUP_ALBUMS);
    query.subject = 3;
    CHECK(album_order_begin(&order, library, query, 0));
    order.group.link_count = 1;
    CHECK(album_order_step(&order));
    CHECK(album_order_step(&order));
    CHECK_EQUAL(UINT16_MAX, order.count);
    uint16_t last = 0;
    CHECK(album_order_read(&order, UINT16_MAX - 1, 1, &last));
    CHECK_EQUAL(UINT16_MAX - 1, last);
    CHECK(album_order_begin(&order, library, query, 0));
    CHECK(album_order_step(&order));
    CHECK(!album_order_step(&order));
    CHECK(order.failed);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_album_plays_in_year_title_order_with_all_its_tracks),
        TEST_CASE(the_queue_accepts_65535_tracks_and_refuses_overflow),
        TEST_CASE(album_artists_play_whole_albums),
        TEST_CASE(shuffle_changes_album_order_and_preserves_track_order),
        TEST_CASE(cue_parts_keep_their_individual_track_indices),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
