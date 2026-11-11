#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "player.h"

static FixtureLibrary fixture;
static Player player;

static bool tracks(void *context, uint16_t first, uint16_t count, uint16_t *output)
{
    (void)context;
    for (uint16_t index = 0; index < count; index++) {
        output[index] = (uint16_t)(first + index);
    }
    return first + count <= 3;
}

static void start(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    player_init(&player, fixture_library_open(&fixture));
    CHECK(player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 0, 0));
    TrackOrder order;
    CHECK_EQUAL(STORE_EMPTY, player_storage_order(&player.storage, &order, NULL));
    TrackOrderJob job = {0};
    CHECK(track_order_job_begin(&job, &order, (TrackOrderSource){tracks, NULL, 3},
                                (TrackOrderEdit){.operation = TRACK_ORDER_COPY}));
    while (track_order_job_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, job.write.result);
    CHECK(player_use_order(&player, &order, 0));
    player.cassette =
        (CassetteState){.generation = 7, .count = 3, .split = 1, .slot = 2, .active = true};
    player_set_repeat(&player, REPEAT_ALL);
    player_toggle_pause(&player);
}

static void side_a_never_prefetches_side_b_and_requires_a_flip(void)
{
    start();
    CHECK(fake_hal_audio_queued_path() == NULL);
    fake_hal_audio_finish_track();
    player_update(&player, 100);
    CHECK(player.cassette.flip);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(player_save(&player));
    player_init(&player, &fixture.library);
    CHECK(player_restore(&player));
    CHECK(player.cassette.active);
    CHECK(player.cassette.flip);
    player_toggle_pause(&player);
    CHECK_EQUAL(1, player.cassette.side);
    CHECK_EQUAL(1, player.track);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(!player.cassette.flip);
}

static void side_b_resumes_its_entry_and_exact_position_after_restart(void)
{
    start();
    fake_hal_audio_finish_track();
    player_update(&player, 100);
    player_toggle_pause(&player);
    fake_hal_audio_set_position(12345);
    CHECK(player_save(&player));
    player_init(&player, &fixture.library);
    player_set_rewind(&player, true);
    CHECK(player_restore(&player));
    CHECK_EQUAL(1, player.cassette.side);
    CHECK_EQUAL(1, player.queue.cursor.position);
    CHECK_EQUAL(12345, player_status(&player).position_ms);
    player_toggle_pause(&player);
    CHECK_EQUAL(12345, fake_hal_audio_play_start());
}

static void side_b_ends_finitely_even_with_repeat_all(void)
{
    start();
    fake_hal_audio_finish_track();
    player_update(&player, 100);
    player_toggle_pause(&player);
    player_skip(&player, 1);
    CHECK_EQUAL(2, player.track);
    CHECK(fake_hal_audio_queued_path() == NULL);
    fake_hal_audio_finish_track();
    player_update(&player, 200);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK_EQUAL(1, player.cassette.side);
    CHECK(player.cassette.flip);
    CHECK_EQUAL(2, player.track);
}

static uint32_t spoken_position(void *context, uint16_t track)
{
    (void)context;
    (void)track;
    return 12345;
}

static void flipping_and_cold_resume_at_zero_do_not_apply_spoken_bookmarks(void)
{
    start();
    LibraryTrack track;
    CHECK(library_read_track(&fixture.library, 1, &track));
    uint32_t path = fixture.library.sections[LIBRARY_STRINGS].offset + track.path;
    strcpy((char *)fixture.bytes + path, "Audiobooks/b.flac");
    player_set_resume(&player, (PlayerResume){spoken_position, NULL});
    fake_hal_audio_finish_track();
    player_update(&player, 100);
    player_toggle_pause(&player);
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    fake_hal_audio_set_position(0);
    CHECK(player_save(&player));
    player_init(&player, &fixture.library);
    player_set_resume(&player, (PlayerResume){spoken_position, NULL});
    CHECK(player_restore(&player));
    player_toggle_pause(&player);
    CHECK_EQUAL(0, fake_hal_audio_play_start());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(side_a_never_prefetches_side_b_and_requires_a_flip),
        TEST_CASE(side_b_resumes_its_entry_and_exact_position_after_restart),
        TEST_CASE(side_b_ends_finitely_even_with_repeat_all),
        TEST_CASE(flipping_and_cold_resume_at_zero_do_not_apply_spoken_bookmarks),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
