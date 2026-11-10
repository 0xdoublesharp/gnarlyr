#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "player.h"
#include "screens/up_next_screen.h"

enum {
    BACK_IN_BLACK = 0,
    HELP = 2,
    ARMY_OF_ME = 3,
    SEED = 7
};

static const char ARMY_OF_ME_PATH[] = "/Various Artists/The Best of Something/02 - Army of Me.flac";
static const char SHOOT_TO_THRILL_PATH[] = "/AC DC/Back In Black/1-02 - Shoot to Thrill.flac";

static FixtureLibrary fixture;
static Player player;
static UpNextScreen up_next;
static Screen *screen;
static Toast toast;

static BrowseQuery back_in_black(void)
{
    return (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = BACK_IN_BLACK,
                         .artist = LIBRARY_NONE};
}

static void play_album_from(uint32_t position)
{
    fake_hal_reset();
    player_init(&player, fixture_library_open(&fixture));
    CHECK(player_play_list(&player, back_in_black(), position, SEED));
}

static void open_up_next(void)
{
    toast = (Toast){0};
    screen = up_next_screen_init(&up_next, (UpNextConfig){.library = &fixture.library,
                                                          .theme = &BOARD_THEME,
                                                          .player = &player,
                                                          .toast = &toast});
    screen->enter(screen->context, 100);
}

static int row_count(void)
{
    return up_next.model.count(up_next.model.context);
}

static bool row_is(int index, const char *label, const char *detail)
{
    const char *shown_label = up_next.model.label(up_next.model.context, index);
    const char *shown_detail = up_next.model.detail(up_next.model.context, index);
    return strcmp(label, shown_label) == 0 && shown_detail != NULL &&
           strcmp(detail, shown_detail) == 0;
}

static ScreenAction press(Key key)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                              200);
}

static ScreenAction choose_row(int index)
{
    for (int step = 0; step < index; step++) {
        press(KEY_VOLUME_UP);
    }
    return press(KEY_PLAY);
}

static bool playing_path_is(const char *expected)
{
    const char *path = fake_hal_audio_playing_path();
    return path != NULL && strcmp(expected, path) == 0;
}

static void the_playing_song_comes_first_then_the_list(void)
{
    play_album_from(0);
    open_up_next();
    CHECK_EQUAL(2, row_count());
    CHECK(row_is(0, "Hells Bells", "Playing"));
    CHECK(row_is(1, "Shoot to Thrill", "AC/DC"));
}

static void a_paused_song_says_so(void)
{
    play_album_from(0);
    player_toggle_pause(&player);
    open_up_next();
    CHECK(row_is(0, "Hells Bells", "Paused"));
}

static void added_songs_come_before_the_list(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    CHECK_EQUAL(3, row_count());
    CHECK(row_is(1, "Help!", "The Beatles"));
    CHECK(row_is(2, "Shoot to Thrill", "AC/DC"));
}

static void the_last_song_has_nothing_after_it(void)
{
    play_album_from(1);
    open_up_next();
    CHECK_EQUAL(1, row_count());
}

static void repeat_all_shows_the_list_wrapping_round(void)
{
    play_album_from(1);
    player_set_repeat(&player, REPEAT_ALL);
    open_up_next();
    CHECK_EQUAL(2, row_count());
    CHECK(row_is(1, "Hells Bells", "AC/DC"));
}

static void choosing_an_added_song_plays_it_and_returns(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    ScreenAction action = choose_row(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK(playing_path_is(ARMY_OF_ME_PATH));
    CHECK_EQUAL(0, player.up_next.count);
}

static void choosing_a_list_song_skips_the_added_ones(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    ScreenAction action = choose_row(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK(playing_path_is(SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(0, player.up_next.count);
}

static void choosing_the_playing_song_just_returns(void)
{
    play_album_from(0);
    open_up_next();
    int plays = fake_hal_audio_play_count();
    ScreenAction action = choose_row(0);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void back_returns_to_now_playing(void)
{
    play_album_from(0);
    open_up_next();
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
}

static void holding_an_added_song_opens_edit_actions(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    press(KEY_VOLUME_UP);
    ScreenAction action = screen->handle_key(
        screen->context, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_LONG_PRESS}, 250);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
}

static Screen *open_actions_on(int row)
{
    list_view_select(&up_next.list, row, 200);
    ScreenAction action = screen->handle_key(
        screen->context, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_LONG_PRESS}, 250);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target != NULL);
    action.target->enter(action.target->context, 250);
    return action.target;
}

static int action_count(void)
{
    return up_next.actions.model.count(up_next.actions.model.context);
}

static const char *action_label(int row)
{
    return up_next.actions.model.label(up_next.actions.model.context, row);
}

static ScreenAction choose_action(Screen *actions, const char *label)
{
    int count = action_count();
    for (int row = 0; row < count; row++) {
        if (strcmp(action_label(row), label) != 0) {
            continue;
        }
        list_view_select(&up_next.actions.list, row, 300);
        actions->update(actions->context, 300);
        return actions->handle_key(actions->context,
                                   (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS}, 300);
    }
    CHECK(false);
    return screen_action_none();
}

static void moving_a_song_returns_to_its_new_row(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    Screen *actions = open_actions_on(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Play Next").type);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(1, up_next.list.selected);
    CHECK(row_is(1, "Army of Me", "Björk"));
    CHECK(row_is(2, "Help!", "The Beatles"));
    CHECK_EQUAL(ARMY_OF_ME, player.up_next.tracks[0]);
}

static void moving_a_song_later_then_earlier_keeps_it_selected(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    Screen *actions = open_actions_on(1);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Move later").type);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(2, up_next.list.selected);
    actions = open_actions_on(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Move earlier").type);
    screen->enter(screen->context, 400);
    CHECK_EQUAL(1, up_next.list.selected);
    CHECK_EQUAL(HELP, player.up_next.tracks[0]);
}

static void removing_the_last_added_song_clamps_selection(void)
{
    play_album_from(1);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    Screen *actions = open_actions_on(1);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Remove").type);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(1, row_count());
    CHECK_EQUAL(0, up_next.list.selected);
    CHECK_EQUAL(0, player.up_next.count);
}

static void holding_the_playing_song_can_clear_only_added_songs(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    Screen *actions = open_actions_on(0);
    CHECK_EQUAL(2, action_count());
    CHECK(strcmp("Clear added", action_label(0)) == 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Clear added").type);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(2, row_count());
    CHECK_EQUAL(0, up_next.list.selected);
    CHECK(row_is(1, "Shoot to Thrill", "AC/DC"));
}

static void generated_rows_offer_editing_with_unavailable_moves_hidden(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    open_actions_on(1);
    CHECK_EQUAL(3, action_count());
    CHECK(strcmp("Move later", action_label(0)) == 0);
    CHECK(strcmp("Remove", action_label(1)) == 0);
    list_view_select(&up_next.list, 3, 300);
    ScreenAction action = screen->handle_key(
        screen->context, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_LONG_PRESS}, 350);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
}

static void a_failed_save_keeps_the_actions_open_for_retry(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    Screen *actions = open_actions_on(1);
    fake_hal_fail_music_writes(true);
    CHECK_EQUAL(SCREEN_ACTION_NONE, choose_action(actions, "Remove").type);
    CHECK_EQUAL(1, player.up_next.count);
    CHECK(toast.visible);
    CHECK(strcmp("Could not save queue", toast.text) == 0);
    fake_hal_fail_music_writes(false);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Remove").type);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(0, player.up_next.count);
}

static void playback_during_an_action_menu_rejects_the_stale_selection(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    Screen *actions = open_actions_on(1);
    int count = action_count();
    fake_hal_audio_finish_track();
    player_update(&player, 275);
    CHECK_EQUAL(count, action_count());
    CHECK(strcmp("Move later", action_label(0)) == 0);
    int writes = fake_hal_write_count();
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Remove").type);
    CHECK_EQUAL(writes, fake_hal_write_count());
    CHECK(toast.visible);
    CHECK(strcmp("Queue changed; try again", toast.text) == 0);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(1, player.up_next.count);
    CHECK_EQUAL(ARMY_OF_ME, player.up_next.tracks[0]);
    CHECK(row_is(0, "Help!", "Playing"));
}

static void playback_refreshes_rows_without_scrolling(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    open_up_next();
    player_toggle_pause(&player);
    screen->update(screen->context, 200);
    CHECK(row_is(0, "Hells Bells", "Paused"));
    player_toggle_pause(&player);
    fake_hal_audio_finish_track();
    player_update(&player, 300);
    screen->update(screen->context, 300);
    CHECK_EQUAL(2, row_count());
    CHECK(row_is(0, "Help!", "Playing"));
    CHECK(row_is(1, "Shoot to Thrill", "AC/DC"));
}

static void backing_out_of_actions_preserves_the_selected_row(void)
{
    play_album_from(0);
    player_add_to_up_next(&player, HELP);
    player_add_to_up_next(&player, ARMY_OF_ME);
    open_up_next();
    Screen *actions = open_actions_on(2);
    ScreenAction action = actions->handle_key(
        actions->context, (KeyEvent){.key = KEY_BACK, .gesture = KEY_GESTURE_PRESS}, 300);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    screen->enter(screen->context, 350);
    CHECK_EQUAL(2, up_next.list.selected);
    CHECK_EQUAL(2, player.up_next.count);
}

static void finish_edit(void)
{
    for (uint32_t step = 0; step < 100 && player_queue_busy(&player); step++) {
        player_update(&player, step);
    }
    CHECK(!player_queue_busy(&player));
    screen->update(screen->context, 500);
}

static void generated_row_removal_is_reachable_without_starting_a_song(void)
{
    play_album_from(0);
    open_up_next();
    int plays = fake_hal_audio_play_count();
    Screen *actions = open_actions_on(1);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Remove").type);
    screen->enter(screen->context, 350);
    CHECK(player_queue_busy(&player));
    finish_edit();
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, player.editing.result);
    CHECK_EQUAL(1, row_count());
    CHECK_EQUAL(0, up_next.list.selected);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK(row_is(0, "Hells Bells", "Playing"));
}

static void the_current_song_menu_can_snapshot_then_keep_only_the_current_song(void)
{
    play_album_from(0);
    open_up_next();
    Screen *actions = open_actions_on(0);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Edit full queue").type);
    screen->enter(screen->context, 350);
    finish_edit();
    CHECK_EQUAL(2, row_count());
    CHECK(player.queue.stored_order != NULL);
    actions = open_actions_on(0);
    CHECK_EQUAL(SCREEN_ACTION_POP, choose_action(actions, "Keep current").type);
    screen->enter(screen->context, 400);
    finish_edit();
    CHECK_EQUAL(1, row_count());
    CHECK_EQUAL(0, player.track);
    open_actions_on(0);
    CHECK_EQUAL(1, action_count());
    CHECK(strcmp("Keep current", action_label(0)) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_playing_song_comes_first_then_the_list),
        TEST_CASE(generated_row_removal_is_reachable_without_starting_a_song),
        TEST_CASE(the_current_song_menu_can_snapshot_then_keep_only_the_current_song),
        TEST_CASE(holding_an_added_song_opens_edit_actions),
        TEST_CASE(moving_a_song_returns_to_its_new_row),
        TEST_CASE(moving_a_song_later_then_earlier_keeps_it_selected),
        TEST_CASE(removing_the_last_added_song_clamps_selection),
        TEST_CASE(holding_the_playing_song_can_clear_only_added_songs),
        TEST_CASE(generated_rows_offer_editing_with_unavailable_moves_hidden),
        TEST_CASE(a_failed_save_keeps_the_actions_open_for_retry),
        TEST_CASE(playback_during_an_action_menu_rejects_the_stale_selection),
        TEST_CASE(playback_refreshes_rows_without_scrolling),
        TEST_CASE(backing_out_of_actions_preserves_the_selected_row),
        TEST_CASE(a_paused_song_says_so),
        TEST_CASE(added_songs_come_before_the_list),
        TEST_CASE(the_last_song_has_nothing_after_it),
        TEST_CASE(repeat_all_shows_the_list_wrapping_round),
        TEST_CASE(choosing_an_added_song_plays_it_and_returns),
        TEST_CASE(choosing_a_list_song_skips_the_added_ones),
        TEST_CASE(choosing_the_playing_song_just_returns),
        TEST_CASE(back_returns_to_now_playing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
