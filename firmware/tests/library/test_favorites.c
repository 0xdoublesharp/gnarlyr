#include "check.h"
#include "fixture_library.h"
#include "library/favorites.h"
#include <string.h>

enum {
    HEADER_BYTES = 8
};

static Favorites favorites;

static Favorites *empty_favorites(void)
{
    favorites_clear(&favorites);
    return &favorites;
}

static void toggling_adds_then_removes(void)
{
    Favorites *list = empty_favorites();
    CHECK(favorites_toggle(list, 42));
    CHECK(favorites_contains(list, 42));
    CHECK(!favorites_toggle(list, 42));
    CHECK(!favorites_contains(list, 42));
    CHECK_EQUAL(0, list->count);
}

static void removing_keeps_the_other_favorites(void)
{
    Favorites *list = empty_favorites();
    favorites_toggle(list, 1);
    favorites_toggle(list, 2);
    favorites_toggle(list, 3);
    favorites_toggle(list, 2);
    CHECK(favorites_contains(list, 1));
    CHECK(!favorites_contains(list, 2));
    CHECK(favorites_contains(list, 3));
    CHECK_EQUAL(2, list->count);
}

static void a_full_list_refuses_new_favorites(void)
{
    Favorites *list = empty_favorites();
    for (uint32_t key = 1; key <= FAVORITES_CAPACITY; key++) {
        favorites_toggle(list, key);
    }
    CHECK(!favorites_toggle(list, 99999));
    CHECK(!favorites_contains(list, 99999));
    CHECK(!favorites_toggle(list, 1));
    CHECK(favorites_toggle(list, 99999));
}

static void favorites_survive_a_round_trip(void)
{
    Favorites *list = empty_favorites();
    favorites_toggle(list, 0xDEADBEEFu);
    favorites_toggle(list, 7);
    uint8_t bytes[FAVORITES_FILE_CAPACITY];
    uint32_t length = favorites_encode(list, bytes);
    CHECK_EQUAL(HEADER_BYTES + 2 * 4 + 4, length);
    Favorites decoded;
    CHECK(favorites_decode(bytes, length, &decoded));
    CHECK_EQUAL(2, decoded.count);
    CHECK(favorites_contains(&decoded, 0xDEADBEEFu));
    CHECK(favorites_contains(&decoded, 7));
}

static void an_empty_list_survives_a_round_trip(void)
{
    uint8_t bytes[FAVORITES_FILE_CAPACITY];
    uint32_t length = favorites_encode(empty_favorites(), bytes);
    Favorites decoded;
    CHECK(favorites_decode(bytes, length, &decoded));
    CHECK_EQUAL(0, decoded.count);
}

static void a_corrupted_file_is_rejected(void)
{
    Favorites *list = empty_favorites();
    favorites_toggle(list, 7);
    uint8_t bytes[FAVORITES_FILE_CAPACITY];
    uint32_t length = favorites_encode(list, bytes);
    bytes[HEADER_BYTES] ^= 0x01;
    Favorites decoded;
    CHECK(!favorites_decode(bytes, length, &decoded));
    CHECK(!favorites_decode(bytes, 3, &decoded));
}

static FixtureLibrary fixture;

static void favorite_albums_are_collected_in_album_order(void)
{
    Library *library = fixture_library_open(&fixture);
    Favorites *list = empty_favorites();
    favorites_toggle(list, library_album_key(library, 3));
    favorites_toggle(list, library_album_key(library, 1));
    uint16_t albums[FIXTURE_ALBUMS];
    CHECK_EQUAL(2, favorites_collect_albums(list, library, albums, FIXTURE_ALBUMS));
    CHECK_EQUAL(1, albums[0]);
    CHECK_EQUAL(3, albums[1]);
}

static void collecting_stops_at_the_capacity(void)
{
    Library *library = fixture_library_open(&fixture);
    Favorites *list = empty_favorites();
    for (uint32_t album = 0; album < FIXTURE_ALBUMS; album++) {
        favorites_toggle(list, library_album_key(library, album));
    }
    uint16_t albums[2];
    CHECK_EQUAL(2, favorites_collect_albums(list, library, albums, 2));
    CHECK_EQUAL(1, albums[1]);
}

static void albums_that_left_the_library_are_not_collected(void)
{
    Library *library = fixture_library_open(&fixture);
    Favorites *list = empty_favorites();
    favorites_toggle(list, 0x12345678u);
    uint16_t albums[FIXTURE_ALBUMS];
    CHECK_EQUAL(0, favorites_collect_albums(list, library, albums, FIXTURE_ALBUMS));
}

static const uint8_t GOLDEN_FAVORITES[] = {
    0x44, 0x53, 0x46, 0x56, 0x01, 0x00, 0x03, 0x00, 0x04, 0x03, 0x02, 0x01,
    0xEF, 0xBE, 0xED, 0xFE, 0xEE, 0xFF, 0xC0, 0x00, 0x5B, 0xCB, 0xE1, 0xCB,
};
static void the_stored_bytes_stay_those_of_the_first_layout(void)
{
    Favorites sample = {.keys = {0x01020304u, 0xFEEDBEEFu, 0x00C0FFEEu}, .count = 3};
    uint8_t bytes[sizeof GOLDEN_FAVORITES];
    CHECK_EQUAL(sizeof GOLDEN_FAVORITES, favorites_encode(&sample, bytes));
    CHECK(memcmp(GOLDEN_FAVORITES, bytes, sizeof bytes) == 0);
    Favorites decoded;
    CHECK(favorites_decode(GOLDEN_FAVORITES, sizeof GOLDEN_FAVORITES, &decoded));
    CHECK_EQUAL(3, decoded.count);
    CHECK_EQUAL(0xFEEDBEEFu, decoded.keys[1]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_stored_bytes_stay_those_of_the_first_layout),
        TEST_CASE(toggling_adds_then_removes),
        TEST_CASE(removing_keeps_the_other_favorites),
        TEST_CASE(a_full_list_refuses_new_favorites),
        TEST_CASE(favorites_survive_a_round_trip),
        TEST_CASE(an_empty_list_survives_a_round_trip),
        TEST_CASE(a_corrupted_file_is_rejected),
        TEST_CASE(favorite_albums_are_collected_in_album_order),
        TEST_CASE(collecting_stops_at_the_capacity),
        TEST_CASE(albums_that_left_the_library_are_not_collected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
