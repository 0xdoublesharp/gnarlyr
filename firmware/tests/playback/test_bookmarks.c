#include "check.h"
#include "playback/bookmarks.h"
#include <string.h>

static Bookmarks bookmarks;
static uint8_t bytes[BOOKMARKS_FILE_CAPACITY];

static Bookmark mark(uint32_t key, uint32_t position_ms)
{
    return (Bookmark){.key = key, .track_key = key + 100, .position_ms = position_ms, .chapter = 2};
}

static Bookmarks *empty(void)
{
    bookmarks_clear(&bookmarks);
    return &bookmarks;
}

static void a_new_list_finds_nothing(void)
{
    Bookmark found;
    CHECK(!bookmarks_find(empty(), 1, &found));
}

static void a_put_bookmark_is_found_by_its_key(void)
{
    Bookmarks *list = empty();
    bookmarks_put(list, mark(7, 1234));
    Bookmark found;
    CHECK(bookmarks_find(list, 7, &found));
    CHECK_EQUAL(1234, found.position_ms);
    CHECK_EQUAL(107, found.track_key);
    CHECK_EQUAL(2, found.chapter);
}

static void putting_the_same_key_replaces_it(void)
{
    Bookmarks *list = empty();
    bookmarks_put(list, mark(7, 1000));
    bookmarks_put(list, mark(7, 2000));
    Bookmark found;
    CHECK_EQUAL(1, list->count);
    CHECK(bookmarks_find(list, 7, &found));
    CHECK_EQUAL(2000, found.position_ms);
}

static void the_latest_bookmark_comes_first(void)
{
    Bookmarks *list = empty();
    bookmarks_put(list, mark(1, 0));
    bookmarks_put(list, mark(2, 0));
    bookmarks_put(list, mark(1, 5));
    CHECK_EQUAL(1, list->entries[0].key);
    CHECK_EQUAL(2, list->entries[1].key);
}

static void a_full_list_forgets_the_oldest_bookmark(void)
{
    Bookmarks *list = empty();
    for (uint32_t key = 1; key <= BOOKMARKS_CAPACITY; key++) {
        bookmarks_put(list, mark(key, key));
    }
    bookmarks_put(list, mark(9999, 1));
    Bookmark found;
    CHECK_EQUAL(BOOKMARKS_CAPACITY, list->count);
    CHECK(!bookmarks_find(list, 1, &found));
    CHECK(bookmarks_find(list, 2, &found));
    CHECK(bookmarks_find(list, 9999, &found));
}

static void finished_bookmarks_say_so(void)
{
    Bookmark done = mark(3, 0);
    done.flags = BOOKMARK_FINISHED;
    CHECK(bookmark_finished(done));
    CHECK(!bookmark_finished(mark(3, 0)));
}

static void bookmarks_survive_a_round_trip(void)
{
    Bookmarks *list = empty();
    Bookmark done = mark(5, 0);
    done.flags = BOOKMARK_FINISHED;
    bookmarks_put(list, mark(4, 4321));
    bookmarks_put(list, done);
    uint32_t length = bookmarks_encode(list, bytes);
    Bookmarks decoded;
    Bookmark found;
    CHECK(bookmarks_decode(bytes, length, &decoded));
    CHECK_EQUAL(2, decoded.count);
    CHECK(bookmarks_find(&decoded, 4, &found));
    CHECK_EQUAL(4321, found.position_ms);
    CHECK(bookmarks_find(&decoded, 5, &found));
    CHECK(bookmark_finished(found));
}

static void a_full_list_fits_the_file(void)
{
    Bookmarks *list = empty();
    for (uint32_t key = 1; key <= BOOKMARKS_CAPACITY; key++) {
        bookmarks_put(list, mark(key, key));
    }
    CHECK_EQUAL(BOOKMARKS_FILE_CAPACITY, bookmarks_encode(list, bytes));
    CHECK(BOOKMARKS_FILE_CAPACITY <= 4096);
}

static void a_damaged_or_empty_file_is_rejected(void)
{
    Bookmarks *list = empty();
    bookmarks_put(list, mark(4, 4321));
    uint32_t length = bookmarks_encode(list, bytes);
    Bookmarks decoded;
    bytes[14] ^= 0xFF;
    CHECK(!bookmarks_decode(bytes, length, &decoded));
    CHECK_EQUAL(0, decoded.count);
    uint8_t zeros[64] = {0};
    CHECK(!bookmarks_decode(zeros, sizeof zeros, &decoded));
    CHECK(!bookmarks_decode(bytes, 3, &decoded));
}

static const uint8_t GOLDEN_BOOKMARKS[] = {
    0x44, 0x53, 0x42, 0x4B, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
    0xD4, 0xC3, 0xB2, 0xA1, 0x91, 0x82, 0x0B, 0x00, 0x03, 0x00, 0x01, 0x00, 0x88, 0x77, 0x66, 0x55,
    0x0D, 0xF0, 0xAD, 0x0B, 0xC0, 0xD4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7A, 0x53, 0x6C, 0xAC,
};
static void the_stored_bytes_stay_those_of_the_first_layout(void)
{
    Bookmarks sample = {.count = 2};
    sample.entries[0] = (Bookmark){0x11223344u, 0xA1B2C3D4u, 754321u, 3, 1};
    sample.entries[1] = (Bookmark){0x55667788u, 0x0BADF00Du, 120000u, 0, 0};
    uint8_t encoded[sizeof GOLDEN_BOOKMARKS];
    CHECK_EQUAL(sizeof GOLDEN_BOOKMARKS, bookmarks_encode(&sample, encoded));
    CHECK(memcmp(GOLDEN_BOOKMARKS, encoded, sizeof encoded) == 0);
    Bookmarks decoded;
    CHECK(bookmarks_decode(GOLDEN_BOOKMARKS, sizeof GOLDEN_BOOKMARKS, &decoded));
    CHECK_EQUAL(2, decoded.count);
    CHECK_EQUAL(0x0BADF00Du, decoded.entries[1].track_key);
    CHECK_EQUAL(3, decoded.entries[0].chapter);
    CHECK_EQUAL(1, decoded.entries[0].flags);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_stored_bytes_stay_those_of_the_first_layout),
        TEST_CASE(a_new_list_finds_nothing),
        TEST_CASE(a_put_bookmark_is_found_by_its_key),
        TEST_CASE(putting_the_same_key_replaces_it),
        TEST_CASE(the_latest_bookmark_comes_first),
        TEST_CASE(a_full_list_forgets_the_oldest_bookmark),
        TEST_CASE(finished_bookmarks_say_so),
        TEST_CASE(bookmarks_survive_a_round_trip),
        TEST_CASE(a_full_list_fits_the_file),
        TEST_CASE(a_damaged_or_empty_file_is_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
