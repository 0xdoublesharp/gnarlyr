#include <string.h>

#include "cassette_art.h"
#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "mixtape_ui_private.h"

static FixtureLibrary fixture;
static Player player;
static MixtapeUi ui;
static Toast toast;
static Screen now_playing;
static Screen up_next;
static Screen *screen;

static void open_ui(void)
{
    screen = mixtape_ui_api()->init(&ui, (MixtapeUiConfig){.player = &player,
                                                           .theme = &BOARD_THEME,
                                                           .toast = &toast,
                                                           .now_playing = &now_playing,
                                                           .up_next = &up_next});
    screen->enter(screen->context, 0);
}

static void choose(int row)
{
    list_view_select(&ui.list, row, 100);
    screen->handle_key(screen->context, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS},
                       100);
}

static void finish(void)
{
    for (int step = 0; step < 2000 && mixtape_ui_api()->busy(&ui); step++) {
        mixtape_ui_api()->step(&ui, 200);
    }
    CHECK(!mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
}

static void create(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    player_init(&player, fixture_library_open(&fixture));
    open_ui();
    choose(2);
    choose(0);
    screen->handle_key(screen->context,
                       (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_LONG_PRESS}, 100);
    finish();
    CHECK_EQUAL(1, ui.count);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK_EQUAL(30, entry.side_minutes);
    CHECK_EQUAL(0, entry.count);
}

static void add(uint16_t track, int side)
{
    open_ui();
    screen = mixtape_ui_api()->add(&ui, track);
    screen->enter(screen->context, 0);
    choose(0);
    choose(side);
    finish();
}

static void tracks_are_inserted_on_the_chosen_side_and_playback_is_finite(void)
{
    create();
    add(0, 0);
    add(1, 1);
    add(2, 0);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK_EQUAL(3, entry.count);
    CHECK_EQUAL(2, entry.side_a_count);
    CHECK(mixtape_store_open(&ui.store, 0));
    while (mixtape_store_step(&ui.store) == STORE_WORKING) {
    }
    MixTapeCursor cursor;
    mix_tape_cursor(&cursor, &ui.store.tape);
    uint16_t expected[] = {0, 2, 1};
    for (int index = 0; index < 3; index++) {
        MixIdentity got, wanted;
        CHECK_EQUAL(STORE_READY, mix_tape_next(&cursor, &got));
        CHECK_EQUAL(STORE_READY, mix_identity_read(player.library, expected[index], &wanted));
        CHECK(mix_identity_equal(&got, &wanted));
    }
    open_ui();
    choose(2);
    choose(1);
    finish();
    CHECK(player.cassette.active);
    CHECK_EQUAL(2, player.cassette.split);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
}

static void a_full_side_is_refused_without_publishing_a_new_generation(void)
{
    create();
    add(0, 0);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    uint32_t generation = entry.generation;
    LibrarySectionInfo tracks = fixture.library.sections[LIBRARY_TRACKS];
    store_put32(fixture.bytes + tracks.offset + tracks.record_size + 16, 1800001);
    add(1, 0);
    CHECK(strcmp(toast.text, i18n_text(STR_TAPE_ADD_FAILED)) == 0);
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK_EQUAL(generation, entry.generation);
    CHECK_EQUAL(1, entry.count);
}

static void a_session_created_after_the_initial_restore_can_start_a_tape(void)
{
    create();
    CHECK(!player_restore(&player, true));
    static uint8_t blank[4096];
    fake_hal_add_file(PLAYER_SESSION_PATH, blank, sizeof blank);
    add(0, 0);
    add(1, 1);
    open_ui();
    choose(2);
    choose(1);
    finish();
    CHECK(player.cassette.active);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
}

static void leaving_the_picker_for_home_does_not_keep_an_old_add_request(void)
{
    create();
    screen = mixtape_ui_api()->add(&ui, 0);
    screen->enter(screen->context, 0);
    CHECK(ui.adding);
    ScreenAction action = screen->handle_key(
        screen->context, (KeyEvent){.key = KEY_BACK, .gesture = KEY_GESTURE_LONG_PRESS}, 100);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, action.type);
    screen->enter(screen->context, 200);
    CHECK(!ui.adding);
    CHECK_EQUAL(4, ui.model.count(&ui));
}

static void both_sides_need_a_song_before_playback(void)
{
    create();
    add(0, 0);
    open_ui();
    choose(2);
    choose(1);
    finish();
    CHECK(!player.cassette.active);
    CHECK(strcmp(toast.text, i18n_text(STR_TAPE_NEEDS_SIDES)) == 0);
}

static void a_c90_tape_keeps_its_forty_five_minute_budget(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    player_init(&player, fixture_library_open(&fixture));
    open_ui();
    choose(2);
    choose(1);
    screen->handle_key(screen->context,
                       (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_LONG_PRESS}, 100);
    finish();
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK_EQUAL(45, entry.side_minutes);
}

static void cassette_budgets_accept_the_limit_and_reject_overflow_and_unknown_lengths(void)
{
    CHECK(cassette_budget(30, 1799000, 1000));
    CHECK(!cassette_budget(30, 1799000, 1001));
    CHECK(cassette_budget(45, 2699000, 1000));
    CHECK(!cassette_budget(45, UINT32_MAX, 1000));
    CHECK(!cassette_budget(30, 0, 0));
    CHECK(!cassette_budget(60, 0, 1000));
    CHECK_EQUAL(cassette_art_pack_size(0, 1), cassette_art_pack_size(0, 0));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(tracks_are_inserted_on_the_chosen_side_and_playback_is_finite),
        TEST_CASE(a_full_side_is_refused_without_publishing_a_new_generation),
        TEST_CASE(both_sides_need_a_song_before_playback),
        TEST_CASE(leaving_the_picker_for_home_does_not_keep_an_old_add_request),
        TEST_CASE(a_session_created_after_the_initial_restore_can_start_a_tape),
        TEST_CASE(a_c90_tape_keeps_its_forty_five_minute_budget),
        TEST_CASE(cassette_budgets_accept_the_limit_and_reject_overflow_and_unknown_lengths),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
