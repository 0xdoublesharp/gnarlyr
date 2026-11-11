#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "gfx/text.h"
#include "mixtape_ui.h"
#include "mixtape_ui_private.h"

static FixtureLibrary fixture;
static Player player;
static MixtapeUi ui;
static Toast toast;
static Screen now_playing;
static Screen up_next;
static Screen *screen;
static WireMotion motion;

static void open_ui(void)
{
    screen = mixtape_ui_api()->init(&ui, (MixtapeUiConfig){.player = &player,
                                                           .theme = &BOARD_THEME,
                                                           .toast = &toast,
                                                           .now_playing = &now_playing,
                                                           .up_next = &up_next,
                                                           .motion = &motion});
    screen->enter(screen->context, 100);
}

static void reset(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    toast = (Toast){0};
    player_init(&player, fixture_library_open(&fixture));
    open_ui();
}

static ScreenAction key(Key key, KeyGesture gesture)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = gesture}, 200);
}

static ScreenAction choose(int row)
{
    list_view_select(&ui.list, row, 200);
    return key(KEY_PLAY, KEY_GESTURE_PRESS);
}

static void finish(void)
{
    for (int step = 0; step < 1000 && mixtape_ui_api()->busy(&ui); step++) {
        CHECK(ui.destination == NULL);
        mixtape_ui_api()->step(&ui, 300);
    }
    CHECK(!mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
}

static void save_album(void)
{
    reset();
    CHECK(player_play_list(&player,
                           (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                                         .group = LIBRARY_ARTISTS,
                                         .subject = 0,
                                         .artist = LIBRARY_NONE},
                           0, 7));
    choose(0);
    CHECK(strcmp(screen->title, "Mixtape name") == 0);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK(mixtape_ui_api()->busy(&ui));
    finish();
    CHECK(strcmp(toast.text, "Mixtape saved") == 0);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK_EQUAL(1, ui.count);
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK(strcmp(entry.name, "Mixtape 1") == 0);
    CHECK_EQUAL(2, entry.count);
}

static void saved_mixtapes_load_as_editable_queues_and_resume_after_restart(void)
{
    save_album();
    CHECK(player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 3, 8));
    open_ui();
    choose(2);
    ScreenAction action = choose(0);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK(ui.destination == NULL);
    finish();
    CHECK(ui.destination == &now_playing);
    CHECK_EQUAL(0, player.track);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(player.queue.stored_order != NULL);
    CHECK_EQUAL(2, player.queue.count);
    player_init(&player, &fixture.library);
    CHECK(player_restore(&player));
    CHECK_EQUAL(0, player.track);
    CHECK_EQUAL(2, player.queue.count);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
}

static void edit_opens_up_next_and_keeps_the_new_queue_paused(void)
{
    save_album();
    open_ui();
    choose(2);
    ScreenAction action = choose(1);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK(ui.destination == NULL);
    finish();
    CHECK(ui.destination == &up_next);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK_EQUAL(0, player.track);
    CHECK_EQUAL(2, player.queue.count);
}

static void a_failed_root_commit_preserves_the_old_queue_and_releases_scratch(void)
{
    save_album();
    CHECK(player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 3, 8));
    uint16_t previous = player.track;
    fake_hal_audio_set_position(4321);
    fake_hal_set_microseconds_per_read(9000);
    open_ui();
    choose(2);
    choose(0);
    for (int step = 0; step < 100 && fake_hal_scratch_outstanding() == 0; step++) {
        mixtape_ui_api()->step(&ui, 300);
    }
    CHECK_EQUAL(1, fake_hal_scratch_outstanding());
    fake_hal_fail_file_writes(PLAYER_SESSION_PATH);
    finish();
    CHECK(ui.destination == NULL);
    CHECK_EQUAL(previous, player.track);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK_EQUAL(4321, player_status(&player).position_ms);
    fake_hal_fail_file_writes(NULL);
    player_init(&player, &fixture.library);
    CHECK(player_restore(&player));
    CHECK_EQUAL(previous, player.track);
    CHECK_EQUAL(4321, player_status(&player).position_ms);
}

static void cancelling_a_load_resumes_the_old_track_at_its_position(void)
{
    save_album();
    fake_hal_audio_set_position(5432);
    fake_hal_set_microseconds_per_read(9000);
    open_ui();
    choose(2);
    choose(0);
    for (int step = 0; step < 100 && fake_hal_scratch_outstanding() == 0; step++) {
        mixtape_ui_api()->step(&ui, 300);
    }
    CHECK_EQUAL(1, fake_hal_scratch_outstanding());
    CHECK(ui.destination == NULL);
    ScreenAction action = key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK_EQUAL(1, fake_hal_scratch_outstanding());
    CHECK(ui.destination == NULL);
    mixtape_ui_api()->cancel(&ui);
    CHECK(ui.destination == NULL);
    CHECK(!mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK_EQUAL(5432, player_status(&player).position_ms);
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void backing_out_of_an_action_returns_to_its_immediate_parent(void)
{
    save_album();
    open_ui();
    choose(2);
    choose(3);
    CHECK_EQUAL(MIXTAPE_NAME, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(MIXTAPE_ACTIONS, ui.mode);
    choose(4);
    CHECK_EQUAL(MIXTAPE_CONFIRM_DELETE, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(MIXTAPE_ACTIONS, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    choose(1);
    CHECK_EQUAL(MIXTAPE_NAME, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void cancelling_a_delete_returns_to_the_list_without_deleting_the_mix(void)
{
    save_album();
    open_ui();
    choose(2);
    choose(4);
    choose(1);
    CHECK(mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK(!mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK_EQUAL(1, ui.count);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK(entry.active);
    CHECK(ui.destination == NULL);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void cancelling_a_name_edit_restores_the_saved_name_in_parent_screens(void)
{
    save_album();
    open_ui();
    choose(2);
    choose(3);
    memcpy(ui.name, "Unsaved draft", sizeof "Unsaved draft");
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(MIXTAPE_ACTIONS, ui.mode);
    CHECK(strcmp(ui.name, "Mixtape 1") == 0);
    CHECK(strcmp(screen->title, "Mixtape 1") == 0);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK(strcmp(entry.name, "Mixtape 1") == 0);
    choose(4);
    CHECK_EQUAL(MIXTAPE_CONFIRM_DELETE, ui.mode);
    CHECK(strcmp(screen->title, "Mixtape 1") == 0);
}

static void cancelling_a_save_or_rename_returns_to_the_list(void)
{
    reset();
    choose(1);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK(mixtape_ui_api()->busy(&ui));
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK(!mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK_EQUAL(0, ui.count);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    save_album();
    open_ui();
    choose(2);
    choose(3);
    memcpy(ui.name, "Changed", sizeof "Changed");
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK(mixtape_ui_api()->busy(&ui));
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK(!mixtape_ui_api()->busy(&ui));
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK(strcmp(entry.name, "Mixtape 1") == 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void editing_and_replacing_a_mixtape_preserves_duplicates_across_a_rescan(void)
{
    save_album();
    open_ui();
    choose(2);
    choose(1);
    finish();
    CHECK_EQUAL(PLAYER_UP_NEXT_WORKING, player_add_to_up_next(&player, 0));
    for (int step = 0; step < 100 && player_queue_busy(&player); step++) {
        player_update(&player, 400);
    }
    CHECK(!player_queue_busy(&player));
    CHECK_EQUAL(3, player.queue.count);
    open_ui();
    choose(2);
    choose(2);
    finish();
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK_EQUAL(3, entry.count);
    LibrarySectionInfo tracks = fixture.library.sections[LIBRARY_TRACKS];
    uint8_t temporary[32];
    CHECK_EQUAL(sizeof temporary, tracks.record_size);
    memcpy(temporary, fixture.bytes + tracks.offset, sizeof temporary);
    memcpy(fixture.bytes + tracks.offset, fixture.bytes + tracks.offset + tracks.record_size,
           sizeof temporary);
    memcpy(fixture.bytes + tracks.offset + tracks.record_size, temporary, sizeof temporary);
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&fixture.library, memory_source(&fixture.memory), fixture.size));
    player_init(&player, &fixture.library);
    CHECK(!player_restore(&player));
    open_ui();
    choose(2);
    choose(1);
    finish();
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK_EQUAL(1, player.track);
    uint16_t resolved[3];
    CHECK(track_order_read(&player.storage.order, 0, 3, resolved));
    CHECK_EQUAL(1, resolved[0]);
    CHECK_EQUAL(0, resolved[1]);
    CHECK_EQUAL(1, resolved[2]);
}

static void an_empty_mixtape_can_be_named_renamed_and_deleted_without_playback(void)
{
    reset();
    choose(1);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK(strcmp(ui.name, "Mixtape 1A") == 0);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    finish();
    CHECK_EQUAL(1, ui.count);
    choose(2);
    choose(3);
    memcpy(ui.name, "Björk", sizeof "Björk");
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    CHECK(strcmp(ui.name, "Bj") == 0);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    finish();
    MixEntry entry;
    CHECK(mixtape_store_read(&ui.store, 0, &entry));
    CHECK(strcmp(entry.name, "Bj") == 0);
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    choose(2);
    choose(0);
    finish();
    CHECK(strcmp(toast.text, "Mixtape is empty") == 0);
    CHECK(!player.has_track);
    CHECK_EQUAL(0, fake_hal_scratch_borrow_count());
    open_ui();
    choose(2);
    choose(4);
    choose(0);
    CHECK_EQUAL(1, ui.count);
    choose(4);
    choose(1);
    finish();
    CHECK_EQUAL(0, ui.count);
    CHECK_EQUAL(MIXTAPE_LIST, ui.mode);
    CHECK(strcmp(toast.text, "Mixtape deleted") == 0);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void a_long_name_keeps_its_tail_visible_without_cutting_a_character(void)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, TEXT_SIZE_LARGE);
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    int width = theme.metrics.screen_width - 2 * theme.metrics.padding;
    const char name[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
    const char *view = mixtape_ui_name_view(name, theme.fonts.body, width);
    CHECK(view > name);
    CHECK(text_width(theme.fonts.body, view) <= width);
    CHECK_EQUAL('5', view[strlen(view) - 1]);
    const char unicode[] = "BjörkBjörkBjörkBjörkBjörkBjörkBjörk";
    view = mixtape_ui_name_view(unicode, theme.fonts.body, width);
    CHECK(view > unicode);
    CHECK(((uint8_t)*view & 0xc0u) != 0x80u);
    CHECK(text_width(theme.fonts.body, view) <= width);
}

static void cancelling_after_completion_discards_only_the_pending_destination(void)
{
    save_album();
    open_ui();
    choose(2);
    CHECK_EQUAL(SCREEN_ACTION_NONE, choose(1).type);
    finish();
    CHECK(ui.destination == &up_next);
    mixtape_ui_api()->cancel(&ui);
    CHECK(ui.destination == NULL);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK_EQUAL(2, player.queue.count);
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(cancelling_after_completion_discards_only_the_pending_destination),
        TEST_CASE(a_long_name_keeps_its_tail_visible_without_cutting_a_character),
        TEST_CASE(saved_mixtapes_load_as_editable_queues_and_resume_after_restart),
        TEST_CASE(edit_opens_up_next_and_keeps_the_new_queue_paused),
        TEST_CASE(a_failed_root_commit_preserves_the_old_queue_and_releases_scratch),
        TEST_CASE(cancelling_a_load_resumes_the_old_track_at_its_position),
        TEST_CASE(backing_out_of_an_action_returns_to_its_immediate_parent),
        TEST_CASE(cancelling_a_delete_returns_to_the_list_without_deleting_the_mix),
        TEST_CASE(cancelling_a_name_edit_restores_the_saved_name_in_parent_screens),
        TEST_CASE(cancelling_a_save_or_rename_returns_to_the_list),
        TEST_CASE(an_empty_mixtape_can_be_named_renamed_and_deleted_without_playback),
        TEST_CASE(editing_and_replacing_a_mixtape_preserves_duplicates_across_a_rescan),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
