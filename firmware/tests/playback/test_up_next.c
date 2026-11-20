#include "check.h"
#include "playback/up_next.h"
#include <string.h>

static const uint32_t FINGERPRINT = 0xCAFEF00Du;

static UpNext with_tracks(const uint16_t *tracks, int count)
{
    UpNext up_next;
    up_next_clear(&up_next);
    for (int index = 0; index < count; index++) {
        CHECK(up_next_add(&up_next, tracks[index]));
    }
    return up_next;
}

static void a_new_list_is_empty(void)
{
    UpNext up_next;
    up_next_clear(&up_next);
    uint16_t track = 0;
    CHECK_EQUAL(0, up_next.count);
    CHECK(!up_next_first(&up_next, &track));
}

static void added_songs_keep_their_order(void)
{
    static const uint16_t TRACKS[] = {7, 3, 9};
    UpNext up_next = with_tracks(TRACKS, 3);
    CHECK_EQUAL(3, up_next.count);
    CHECK_EQUAL(7, up_next.tracks[0]);
    CHECK_EQUAL(3, up_next.tracks[1]);
    CHECK_EQUAL(9, up_next.tracks[2]);
}

static void play_next_goes_to_the_front(void)
{
    static const uint16_t TRACKS[] = {7, 3};
    UpNext up_next = with_tracks(TRACKS, 2);
    CHECK(up_next_play_next(&up_next, 11));
    uint16_t track = 0;
    CHECK(up_next_first(&up_next, &track));
    CHECK_EQUAL(11, track);
    CHECK_EQUAL(3, up_next.count);
    CHECK_EQUAL(7, up_next.tracks[1]);
    CHECK_EQUAL(3, up_next.tracks[2]);
}

static void a_full_list_refuses_more(void)
{
    UpNext up_next;
    up_next_clear(&up_next);
    for (int index = 0; index < UP_NEXT_CAPACITY; index++) {
        CHECK(up_next_add(&up_next, (uint16_t)index));
    }
    CHECK(!up_next_add(&up_next, 500));
    CHECK(!up_next_play_next(&up_next, 500));
    CHECK_EQUAL(UP_NEXT_CAPACITY, up_next.count);
    CHECK_EQUAL(UP_NEXT_CAPACITY - 1, up_next.tracks[UP_NEXT_CAPACITY - 1]);
}

static void dropping_removes_from_the_front(void)
{
    static const uint16_t TRACKS[] = {7, 3, 9, 4};
    UpNext up_next = with_tracks(TRACKS, 4);
    up_next_drop(&up_next, 2);
    CHECK_EQUAL(2, up_next.count);
    CHECK_EQUAL(9, up_next.tracks[0]);
    CHECK_EQUAL(4, up_next.tracks[1]);
    up_next_drop(&up_next, 5);
    CHECK_EQUAL(0, up_next.count);
}

static void a_list_survives_a_round_trip(void)
{
    static const uint16_t TRACKS[] = {7, 3, 1200};
    UpNext saved = with_tracks(TRACKS, 3);
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&saved, FINGERPRINT, bytes);
    UpNext loaded;
    CHECK(up_next_decode(bytes, sizeof bytes, FINGERPRINT, &loaded));
    CHECK_EQUAL(3, loaded.count);
    CHECK_EQUAL(7, loaded.tracks[0]);
    CHECK_EQUAL(3, loaded.tracks[1]);
    CHECK_EQUAL(1200, loaded.tracks[2]);
}

static void another_library_is_rejected(void)
{
    static const uint16_t TRACKS[] = {7};
    UpNext saved = with_tracks(TRACKS, 1);
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&saved, FINGERPRINT, bytes);
    UpNext loaded;
    CHECK(!up_next_decode(bytes, sizeof bytes, FINGERPRINT + 1, &loaded));
}

static void a_corrupted_or_blank_file_is_rejected(void)
{
    static const uint16_t TRACKS[] = {7, 3};
    UpNext saved = with_tracks(TRACKS, 2);
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&saved, FINGERPRINT, bytes);
    bytes[16] ^= 1;
    UpNext loaded;
    CHECK(!up_next_decode(bytes, sizeof bytes, FINGERPRINT, &loaded));
    uint8_t blank[UP_NEXT_STATE_SIZE] = {0};
    CHECK(!up_next_decode(blank, sizeof blank, FINGERPRINT, &loaded));
    CHECK(!up_next_decode(bytes, UP_NEXT_STATE_SIZE - 1, FINGERPRINT, &loaded));
}

static const uint8_t GOLDEN_UP_NEXT[] = {
    0x44, 0x53, 0x55, 0x4E, 0x01, 0x00, 0x03, 0x00, 0x78, 0x56, 0x34, 0x12, 0x07, 0x00, 0x92, 0x10,
    0xE8, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xAB, 0x7A, 0x0E, 0x84,
};
static void the_stored_bytes_stay_those_of_the_first_layout(void)
{
    UpNext sample = {.tracks = {7, 4242, 65000}, .count = 3};
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&sample, 0x12345678u, bytes);
    CHECK_EQUAL(sizeof GOLDEN_UP_NEXT, sizeof bytes);
    CHECK(memcmp(GOLDEN_UP_NEXT, bytes, sizeof bytes) == 0);
    UpNext decoded;
    CHECK(up_next_decode(GOLDEN_UP_NEXT, sizeof GOLDEN_UP_NEXT, 0x12345678u, &decoded));
    CHECK_EQUAL(3, decoded.count);
    CHECK_EQUAL(65000, decoded.tracks[2]);
    CHECK(!up_next_decode(GOLDEN_UP_NEXT, sizeof GOLDEN_UP_NEXT, 0x12345679u, &decoded));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_stored_bytes_stay_those_of_the_first_layout),
        TEST_CASE(a_new_list_is_empty),
        TEST_CASE(added_songs_keep_their_order),
        TEST_CASE(play_next_goes_to_the_front),
        TEST_CASE(a_full_list_refuses_more),
        TEST_CASE(dropping_removes_from_the_front),
        TEST_CASE(a_list_survives_a_round_trip),
        TEST_CASE(another_library_is_rejected),
        TEST_CASE(a_corrupted_or_blank_file_is_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
