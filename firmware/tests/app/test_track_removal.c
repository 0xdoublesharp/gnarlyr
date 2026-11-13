#include <string.h>

#include "browse_screen.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"
#include "hidden_scan.h"
#include "hidden_tracks.h"
#include "library/library_browse.h"
#include "library_store.h"
#include "player.h"
#include "settings_screens.h"
#include "track_removal.h"

enum {
    HELLS_BELLS = 0,
    SHOOT_TO_THRILL = 1,
    HELP = 2,
    ARMY_OF_ME = 3,
    BACK_IN_BLACK = 0,
    BEST_OF = 1,
    CUE_MIDDLE = 3,
    SEED = 7
};

static const char HELLS_BELLS_PATH[] = "/AC DC/Back In Black/1-01 - Hells Bells.flac";
static const char SHOOT_TO_THRILL_PATH[] = "/AC DC/Back In Black/1-02 - Shoot to Thrill.flac";
static const char HELP_RELATIVE[] = "Various Artists/The Best of Something/01 - Help!.flac";
static const char ARMY_RELATIVE[] = "Various Artists/The Best of Something/02 - Army of Me.flac";

static LibraryStore store;
static Player player;
static FixtureLibrary fixture;
static Theme theme;
static Toast toast;
static BrowsePool pool;

static HiddenTracks tracks(void)
{
    return (HiddenTracks){.store = &store, .player = &player};
}

static void open_store(void)
{
    CHECK(library_store_open(&store));
    player_init(&player, &store.library);
}

static void start(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_reset();
    theme = BOARD_THEME;
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", bytes, size);
    open_store();
}

static BrowseQuery album(uint16_t subject)
{
    BrowseQuery query = browse_root_query(BROWSE_ALBUM_TRACKS);
    query.subject = subject;
    return query;
}

static bool lists(BrowseQuery query, uint16_t track)
{
    uint32_t count = browse_count(&store.library, query);
    for (uint32_t position = 0; position < count; position++) {
        BrowseItem item;
        CHECK(browse_item(&store.library, query, position, &item));
        if (item.kind == BROWSE_ITEM_TRACK && item.index == track) {
            return true;
        }
    }
    return false;
}

static uint16_t hidden_records(void)
{
    HiddenTracksHead head;
    return hidden_tracks_read_head(&head);
}

static HiddenScan scan;

static bool scan_skips(const char *path)
{
    BuilderTrack track = {.path = path};
    return hidden_scan_skips(&scan, &track);
}

static void only_music_paths_may_be_deleted(void)
{
    CHECK(track_removal_path_allowed(HELP_RELATIVE));
    CHECK(track_removal_path_allowed("song.flac"));
    CHECK(!track_removal_path_allowed(""));
    CHECK(!track_removal_path_allowed("/song.flac"));
    CHECK(!track_removal_path_allowed("." FIRMWARE_DATA_DIRECTORY "/library.db"));
    CHECK(!track_removal_path_allowed("Album/../." FIRMWARE_DATA_DIRECTORY "/library.db"));
    CHECK(!track_removal_path_allowed("Album/.hidden.flac"));
    CHECK(!track_removal_path_allowed("Album//song.flac"));
    CHECK(!track_removal_path_allowed("Album/"));
    CHECK(!track_removal_path_allowed("Album\\song.flac"));
    CHECK(!track_removal_path_allowed("C:song.flac"));
}

static void a_hidden_track_leaves_every_list(void)
{
    start();
    uint32_t songs = browse_count(&store.library, browse_root_query(BROWSE_ALL_SONGS));
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    CHECK(library_hidden(&store.library, HELP));
    CHECK_EQUAL(songs - 1, browse_count(&store.library, browse_root_query(BROWSE_ALL_SONGS)));
    CHECK(!lists(browse_root_query(BROWSE_ALL_SONGS), HELP));
    CHECK(lists(browse_root_query(BROWSE_ALL_SONGS), ARMY_OF_ME));
    CHECK(!lists(album(BEST_OF), HELP));
    CHECK(lists(album(BEST_OF), ARMY_OF_ME));
    BrowseQuery tree = browse_root_query(BROWSE_FOLDER_TREE);
    CHECK(!lists(tree, HELP));
    CHECK_EQUAL(songs - 1, browse_count(&store.library, tree));
}

static void a_hidden_track_is_hidden_again_after_a_restart(void)
{
    start();
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    open_store();
    CHECK(library_hidden(&store.library, HELP));
    CHECK(!lists(album(BEST_OF), HELP));
    CHECK_EQUAL(1, hidden_records());
}

static void a_rescan_leaves_out_hidden_tracks_until_they_are_shown(void)
{
    start();
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    hidden_scan_load(&scan);
    CHECK(scan_skips(HELP_RELATIVE));
    CHECK(!scan_skips(ARMY_RELATIVE));
    hidden_scan_forget_runtime();
    open_store();
    CHECK(!library_hidden(&store.library, HELP));
    CHECK_EQUAL(1, hidden_records());
    hidden_scan_load(&scan);
    CHECK(scan_skips(HELP_RELATIVE));
    CHECK_EQUAL(HIDDEN_REFUSED, hidden_tracks_unhide(tracks(), 0));
    CHECK_EQUAL(0, hidden_records());
    hidden_scan_load(&scan);
    CHECK(!scan_skips(HELP_RELATIVE));
}

static void hidden_storage_rejects_missing_corrupt_and_unwritable_records(void)
{
    fake_hal_reset();
    HiddenTracksHead head;
    MixIdentity identity;
    CHECK_EQUAL(0, hidden_tracks_read_head(&head));
    CHECK(!hidden_tracks_read(0, &identity));

    HiddenTracksHead corrupt = {.magic = 1, .records = 1};
    fake_hal_add_file(HIDDEN_TRACKS_PATH, (const uint8_t *)&corrupt, sizeof corrupt);
    CHECK_EQUAL(0, hidden_tracks_read_head(&head));
    corrupt.magic = HIDDEN_TRACKS_MAGIC;
    corrupt.records = HIDDEN_TRACKS_RECORDS + 1;
    fake_hal_reset();
    fake_hal_add_file(HIDDEN_TRACKS_PATH, (const uint8_t *)&corrupt, sizeof corrupt);
    CHECK_EQUAL(0, hidden_tracks_read_head(&head));

    fake_hal_reset();
    fake_hal_add_file(HIDDEN_TRACKS_PATH, NULL, HIDDEN_TRACKS_FILE_BYTES);
    CHECK(!hidden_tracks_read(0, &identity));
    CHECK(!hidden_tracks_write(0, &(MixIdentity){0}));
    fake_hal_fail_file_writes(HIDDEN_TRACKS_PATH);
    CHECK(!hidden_tracks_write(0, &(MixIdentity){.path = "Album/song.flac"}));
}

static void hidden_prepare_and_membership_enforce_their_bounds(void)
{
    HiddenTracksHead head;
    CHECK(!hidden_tracks_prepare((HiddenTracks){0}, &head));
    start();
    store.hidden = (LibraryHidden){0};
    store.library.hidden = &store.hidden;
    store.available = false;
    CHECK(!hidden_tracks_prepare(tracks(), &head));
    store.available = true;
    fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
    CHECK(!hidden_tracks_prepare(tracks(), &head));
    fake_hal_set_reserve_result(HAL_RESERVE_EXISTS);
    CHECK(hidden_tracks_prepare(tracks(), &head));

    hidden_tracks_exclude(tracks(), HELP);
    CHECK_EQUAL(1, store.hidden.count);
    hidden_tracks_exclude(tracks(), HELP);
    CHECK_EQUAL(1, store.hidden.count);
    hidden_tracks_include(tracks(), ARMY_OF_ME);
    CHECK_EQUAL(1, store.hidden.count);
    store.hidden.count = LIBRARY_HIDDEN_CAPACITY;
    hidden_tracks_exclude(tracks(), ARMY_OF_ME);
    CHECK_EQUAL(LIBRARY_HIDDEN_CAPACITY, store.hidden.count);

    player.has_track = false;
    player.track = HELP;
    hidden_tracks_replan(&player, HELP);
    CHECK_EQUAL(HELP, hidden_tracks_anchor(&player));
}

static void unhide_reports_record_and_file_update_failures(void)
{
    start();
    CHECK_EQUAL(HIDDEN_FAILED, hidden_tracks_unhide(tracks(), 0));

    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), ARMY_OF_ME));
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    fake_hal_fail_file_writes(HIDDEN_TRACKS_PATH);
    CHECK_EQUAL(HIDDEN_FAILED, hidden_tracks_unhide(tracks(), 0));

    start();
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    fake_hal_fail_file_writes(HIDDEN_TRACKS_PATH);
    CHECK_EQUAL(HIDDEN_FAILED, hidden_tracks_unhide(tracks(), 0));
}

static void showing_a_track_hidden_since_the_last_rescan_lists_it_again(void)
{
    start();
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), ARMY_OF_ME));
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    CHECK_EQUAL(HIDDEN_DONE, hidden_tracks_unhide(tracks(), 0));
    CHECK(!library_hidden(&store.library, ARMY_OF_ME));
    CHECK(lists(album(BEST_OF), ARMY_OF_ME));
    CHECK(library_hidden(&store.library, HELP));
    open_store();
    CHECK(!library_hidden(&store.library, ARMY_OF_ME));
    CHECK(library_hidden(&store.library, HELP));
    CHECK_EQUAL(1, hidden_records());
    MixIdentity left;
    CHECK(hidden_tracks_read(0, &left));
    CHECK(strcmp(HELP_RELATIVE, left.path) == 0);
}

static void deleting_the_playing_track_moves_on_and_removes_the_file(void)
{
    start();
    fake_hal_add_file(HELLS_BELLS_PATH, NULL, 100);
    CHECK(player_play_list(&player, album(BACK_IN_BLACK), 0, SEED));
    CHECK_EQUAL(HIDDEN_DONE, track_removal_delete(tracks(), HELLS_BELLS));
    CHECK_EQUAL(SHOOT_TO_THRILL, player_status(&player).track);
    CHECK(strcmp(SHOOT_TO_THRILL_PATH, fake_hal_audio_playing_path()) == 0);
    HalFile file;
    CHECK(!hal_file_open(&file, HELLS_BELLS_PATH));
    CHECK(!lists(album(BACK_IN_BLACK), HELLS_BELLS));
    CHECK_EQUAL(1, player_status(&player).place.total);
    CHECK_EQUAL(0, hidden_records());
}

static void deleting_a_queued_track_takes_it_out_of_the_queue(void)
{
    start();
    fake_hal_add_file(SHOOT_TO_THRILL_PATH, NULL, 100);
    CHECK(player_play_list(&player, album(BACK_IN_BLACK), 0, SEED));
    CHECK(strcmp(SHOOT_TO_THRILL_PATH, fake_hal_audio_queued_path()) == 0);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(&player, SHOOT_TO_THRILL));
    CHECK_EQUAL(HIDDEN_DONE, track_removal_delete(tracks(), SHOOT_TO_THRILL));
    CHECK_EQUAL(HELLS_BELLS, player_status(&player).track);
    CHECK(fake_hal_audio_queued_path() == NULL);
    CHECK_EQUAL(0, player.up_next.count);
    CHECK_EQUAL(1, player_status(&player).place.total);
    fake_hal_audio_finish_track();
    player_update(&player, 100);
    CHECK_EQUAL(PLAYER_IDLE, player_status(&player).state);
}

static void a_file_that_cannot_be_removed_stays_listed(void)
{
    start();
    CHECK(player_play_list(&player, album(BACK_IN_BLACK), 0, SEED));
    CHECK_EQUAL(HIDDEN_FAILED, track_removal_delete(tracks(), SHOOT_TO_THRILL));
    CHECK(!library_hidden(&store.library, SHOOT_TO_THRILL));
    CHECK(lists(album(BACK_IN_BLACK), SHOOT_TO_THRILL));
    CHECK(strcmp(SHOOT_TO_THRILL_PATH, fake_hal_audio_queued_path()) == 0);
}

static void part_of_a_cue_file_is_not_deleted(void)
{
    fake_hal_reset();
    store = (LibraryStore){.library = *fixture_cue_library_open(&fixture), .available = true};
    player_init(&player, &store.library);
    CHECK_EQUAL(HIDDEN_REFUSED, track_removal_delete(tracks(), CUE_MIDDLE));
    CHECK(!library_hidden(&store.library, CUE_MIDDLE));
}

static Screen *no_screen(void *context, uint16_t track, SongManage manage)
{
    (void)context;
    (void)track;
    (void)manage;
    return NULL;
}

static Screen *song_actions(uint16_t track)
{
    browse_pool_init(&pool, (BrowsePoolConfig){.library = &store.library,
                                               .theme = &theme,
                                               .player = &player,
                                               .toast = &toast,
                                               .manage = no_screen});
    return browse_pool_song_actions(&pool, track, "Song", 0);
}

static void the_delete_question_names_the_file_and_starts_on_cancel(void)
{
    start();
    CHECK(song_actions(HELP) != NULL);
    TrackRemoval removal = {.tracks = tracks(), .actions = &pool.song_actions, .toast = &toast};
    Screen *screen = track_removal_manage(removal, HELP, SONG_MANAGE_ASK);
    CHECK(screen == &pool.song_actions.menu.screen);
    CHECK(strcmp("01 - Help!.flac", pool.song_actions.menu.config.title) == 0);
    CHECK_EQUAL(2, pool.song_actions.menu.config.item_count);
    CHECK_EQUAL(0, pool.song_actions.menu.list.selected);
    CHECK(strcmp("Cancel", pool.song_actions.items[0].label) == 0);
    CHECK(strcmp("Delete file", pool.song_actions.items[1].label) == 0);
    CHECK(pool.song_actions.confirming);
    CHECK(track_removal_manage(removal, HELP, SONG_MANAGE_KEEP) == NULL);
    CHECK(!library_hidden(&store.library, HELP));
}

static void the_hidden_list_names_each_file_and_play_shows_it_again(void)
{
    static SettingsScreens screens;
    static SettingsStore settings;
    start();
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), ARMY_OF_ME));
    CHECK_EQUAL(HIDDEN_DONE, track_removal_hide(tracks(), HELP));
    settings.settings = settings_defaults();
    SettingsContext context = {
        .store = &settings, .player = &player, .toast = &toast, .library = &store, .theme = &theme};
    (void)settings_screens_init(&screens, context, &theme);
    MenuItem row = screens.items[SETTINGS_HIDDEN_ITEM];
    CHECK(row.shown(row.shown_context));
    ScreenAction opened = row.select(row.context);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, opened.type);
    ChoiceScreen *choice = &screens.hidden.choice;
    CHECK_EQUAL(2, choice->config.option_count);
    CHECK(strcmp("02 - Army of Me.flac", choice->config.options[0]) == 0);
    CHECK(strcmp("01 - Help!.flac", choice->config.options[1]) == 0);
    KeyEvent play = {.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS};
    opened.target->enter(opened.target->context, 0);
    ScreenAction shown = opened.target->handle_key(opened.target->context, play, 10);
    CHECK_EQUAL(SCREEN_ACTION_REPLACE, shown.type);
    CHECK(!library_hidden(&store.library, ARMY_OF_ME));
    CHECK_EQUAL(1, choice->config.option_count);
    CHECK(strcmp("01 - Help!.flac", choice->config.options[0]) == 0);
    shown = opened.target->handle_key(opened.target->context, play, 20);
    CHECK_EQUAL(SCREEN_ACTION_POP, shown.type);
    CHECK(!row.shown(row.shown_context));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(only_music_paths_may_be_deleted),
        TEST_CASE(a_hidden_track_leaves_every_list),
        TEST_CASE(a_hidden_track_is_hidden_again_after_a_restart),
        TEST_CASE(a_rescan_leaves_out_hidden_tracks_until_they_are_shown),
        TEST_CASE(hidden_storage_rejects_missing_corrupt_and_unwritable_records),
        TEST_CASE(hidden_prepare_and_membership_enforce_their_bounds),
        TEST_CASE(unhide_reports_record_and_file_update_failures),
        TEST_CASE(showing_a_track_hidden_since_the_last_rescan_lists_it_again),
        TEST_CASE(deleting_the_playing_track_moves_on_and_removes_the_file),
        TEST_CASE(deleting_a_queued_track_takes_it_out_of_the_queue),
        TEST_CASE(a_file_that_cannot_be_removed_stays_listed),
        TEST_CASE(part_of_a_cue_file_is_not_deleted),
        TEST_CASE(the_delete_question_names_the_file_and_starts_on_cancel),
        TEST_CASE(the_hidden_list_names_each_file_and_play_shows_it_again),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
