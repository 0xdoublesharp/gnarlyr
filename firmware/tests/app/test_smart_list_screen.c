#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "favorites_store.h"
#include "fixture_library.h"
#include "listening_fixture.h"
#include "playback/track_keys.h"
#include "player_storage.h"
#include "screen_snapshot.h"
#include "screens/listening_work.h"
#include "screens/smart_list_screen.h"
#include "text_spy.h"

static FixtureLibrary fixture;
static ListeningDisk disk;
static ListeningStore store;
static FavoritesStore favorites;
static LibraryExtraPlaylist extra;
static BrowsePool pool;
static Player player;
static Toast toast;
static Screen now_playing;
static SmartListScreen smart;
static Theme theme;
static uint32_t clock_ms;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static void attach(void)
{
    listening_store_attach(&store, &fixture.library, listening_disk_file(&disk, sizeof disk.bytes));
    favorites = (FavoritesStore){.library = &fixture.library};
    player_init(&player, &fixture.library);
    player_restore(&player, true);
    browse_pool_init(&pool, (BrowsePoolConfig){.library = &fixture.library,
                                               .theme = &theme,
                                               .player = &player,
                                               .now_playing = &now_playing,
                                               .favorites = &favorites,
                                               .toast = &toast});
    smart_list_screen_init(&smart, (SmartListScreenConfig){&store, &pool});
}

static void start_with(bool keys_file)
{
    fake_hal_reset();
    fixture_library_open(&fixture);
    if (keys_file) {
        fake_hal_add_file(TRACK_KEYS_PATH, NULL, TRACK_KEYS_FILE_BYTES);
    }
    fake_hal_add_file(PLAYER_SESSION_PATH, NULL, 4096);
    fake_hal_add_file(FAVORITES_STORE_PATH, NULL, 4096);
    listening_disk_reset(&disk);
    theme = BOARD_THEME;
    toast = (Toast){0};
    clock_ms = 0;
    attach();
}

static void start(void)
{
    start_with(true);
}

static void add_play(uint16_t index)
{
    ListeningTrack track;
    CHECK_EQUAL(STORE_READY, listening_track_read(&fixture.library, index, &track));
    ListeningPlay play = {.track = track,
                          .length_ms = 60000,
                          .listened_ms = 60000,
                          .end_ms = 60000,
                          .end = LISTENING_FINISHED,
                          .output = LISTENING_OUTPUT_SINGLE_ENDED,
                          .format = LISTENING_FORMAT_LOSSLESS};
    CHECK_EQUAL(STORE_DONE, listening_log_append(&store.log, &play));
}

static void tick(void)
{
    smart.screen.update(&smart, ++clock_ms);
}

static bool working(void)
{
    return smart.status == 0;
}

static void finish(void)
{
    for (int step = 0; step < 20000 && working(); step++) {
        tick();
    }
    CHECK(!working());
}

static void open_list(SmartListKind kind)
{
    ScreenAction action = smart.items[kind].select(smart.items[kind].context);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &smart.screen);
    smart.screen.enter(&smart, ++clock_ms);
}

static void show(SmartListKind kind)
{
    open_list(kind);
    finish();
    tick();
}

static ScreenAction key(Key button, KeyGesture gesture)
{
    return smart.screen.handle_key(&smart, (KeyEvent){.key = button, .gesture = gesture},
                                   ++clock_ms);
}

static Canvas draw(void)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    smart.screen.draw(&smart, &canvas);
    return canvas;
}

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strstr(text_spy_entry(index)->rest, text) != NULL) {
            return true;
        }
    }
    return false;
}

static int rows(void)
{
    return smart.model.count(&smart);
}

static void title_of(uint16_t track, char *title, char *artist)
{
    LibraryTrack record;
    LibraryGroup group;
    CHECK(library_read_track(&fixture.library, track, &record));
    CHECK(library_read_string(&fixture.library, record.title, title, 128));
    CHECK(library_read_group(&fixture.library, LIBRARY_ARTISTS, record.artist, &group));
    CHECK(library_read_string(&fixture.library, group.name, artist, 128));
}

static void check_row(int row, uint16_t track)
{
    char title[128];
    char artist[128];
    title_of(track, title, artist);
    CHECK_EQUAL(track, smart.caches[smart.kind].tracks[row]);
    CHECK(strcmp(title, smart.model.label(&smart, row)) == 0);
    CHECK(strcmp(artist, smart.model.detail(&smart, row)) == 0);
}

static void recently_played_shows_songs_newest_first_with_their_artist(void)
{
    start();
    add_play(0);
    add_play(4);
    add_play(0);
    add_play(2);
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(3, rows());
    check_row(0, 2);
    check_row(1, 0);
    check_row(2, 4);
    draw();
    CHECK(saw(i18n_text(STR_RECENTLY_PLAYED)));
}

static void each_list_has_its_own_empty_state(void)
{
    static const StringId EMPTY[SMART_LIST_KIND_COUNT] = {
        STR_SMART_NONE_ADDED, STR_SMART_NONE_PLAYED,    STR_SMART_NONE_LISTENED,
        STR_SMART_ALL_PLAYED, STR_SMART_NONE_FORGOTTEN,
    };
    start();
    for (uint16_t track = 0; track < library_music_count(&fixture.library, LIBRARY_TRACKS);
         track++) {
        add_play(track);
    }
    for (int kind = 0; kind < SMART_LIST_KIND_COUNT; kind++) {
        if (kind == SMART_LIST_NEVER_PLAYED || kind == SMART_LIST_RECENTLY_ADDED ||
            kind == SMART_LIST_FORGOTTEN) {
            show((SmartListKind)kind);
            CHECK_EQUAL(0, rows());
            draw();
            CHECK(saw(i18n_text(EMPTY[kind])));
        }
    }
    start();
    for (int kind = 0; kind < SMART_LIST_KIND_COUNT; kind++) {
        if (kind != SMART_LIST_NEVER_PLAYED) {
            show((SmartListKind)kind);
            CHECK_EQUAL(0, rows());
            draw();
            CHECK(saw(i18n_text(EMPTY[kind])));
        }
    }
}

static void the_first_list_checks_the_library_and_says_so(void)
{
    start();
    open_list(SMART_LIST_NEVER_PLAYED);
    CHECK(working());
    CHECK(smart.checking);
    draw();
    CHECK(saw("Checking library"));
    finish();
    CHECK_EQUAL((int)library_music_count(&fixture.library, LIBRARY_TRACKS), rows());
    open_list(SMART_LIST_RECENTLY_PLAYED);
    CHECK(working());
    CHECK(!smart.checking);
    draw();
    CHECK(saw("Finding songs"));
}

static void a_list_is_kept_until_a_play_is_logged(void)
{
    start();
    add_play(1);
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(1, rows());
    key(KEY_BACK, KEY_GESTURE_PRESS);
    open_list(SMART_LIST_RECENTLY_PLAYED);
    CHECK(!working());
    CHECK_EQUAL(1, rows());
    add_play(3);
    open_list(SMART_LIST_RECENTLY_PLAYED);
    CHECK(working());
    finish();
    CHECK_EQUAL(2, rows());
    CHECK_EQUAL(3, smart.caches[SMART_LIST_RECENTLY_PLAYED].tracks[0]);
}

static void a_favorite_change_refreshes_old_favorites(void)
{
    start();
    show(SMART_LIST_FORGOTTEN);
    CHECK_EQUAL(0, rows());
    favorites_store_toggle_album(&favorites, 0);
    show(SMART_LIST_FORGOTTEN);
    LibraryAlbum album;
    CHECK(library_read_album(&fixture.library, 0, &album));
    CHECK_EQUAL(album.track_count, rows());
    CHECK_EQUAL(album.first_track, smart.caches[SMART_LIST_FORGOTTEN].tracks[0]);
}

static void old_favorites_lists_long_unplayed_favorites_first(void)
{
    start();
    extra = (LibraryExtraPlaylist){.count = 2, .tracks = {5, 1}};
    fixture.library.extra = &extra;
    add_play(1);
    for (int play = 0; play < SMART_LIST_FORGOTTEN_WINDOW; play++) {
        add_play(7);
    }
    show(SMART_LIST_FORGOTTEN);
    CHECK_EQUAL(2, rows());
    CHECK_EQUAL(1, smart.caches[SMART_LIST_FORGOTTEN].tracks[0]);
    CHECK_EQUAL(5, smart.caches[SMART_LIST_FORGOTTEN].tracks[1]);
    add_play(5);
    show(SMART_LIST_FORGOTTEN);
    CHECK_EQUAL(1, rows());
    CHECK_EQUAL(1, smart.caches[SMART_LIST_FORGOTTEN].tracks[0]);
}

static void songs_new_after_a_library_change_are_recently_added(void)
{
    start();
    show(SMART_LIST_RECENTLY_ADDED);
    CHECK_EQUAL(0, rows());
    fixture_cue_library_open(&fixture);
    attach();
    open_list(SMART_LIST_RECENTLY_ADDED);
    CHECK(smart.checking);
    finish();
    CHECK_EQUAL((int)library_music_count(&fixture.library, LIBRARY_TRACKS), rows());
    for (int row = 1; row < rows(); row++) {
        CHECK(smart.caches[SMART_LIST_RECENTLY_ADDED].tracks[row - 1] <
              smart.caches[SMART_LIST_RECENTLY_ADDED].tracks[row]);
    }
}

static void the_parts_of_a_cue_file_are_separate_songs(void)
{
    start();
    fixture_cue_library_open(&fixture);
    attach();
    add_play(0);
    add_play(1);
    add_play(0);
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(2, rows());
    CHECK_EQUAL(0, smart.caches[SMART_LIST_RECENTLY_PLAYED].tracks[0]);
    CHECK_EQUAL(1, smart.caches[SMART_LIST_RECENTLY_PLAYED].tracks[1]);
}

static void playing_a_list_queues_its_exact_order_and_opens_now_playing(void)
{
    start();
    add_play(2);
    add_play(0);
    add_play(4);
    show(SMART_LIST_RECENTLY_PLAYED);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    ScreenAction action = key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &now_playing);
    PlayerStatus status = player_status(&player);
    CHECK(status.has_track);
    CHECK_EQUAL(0, status.track);
    CHECK(player.queue.stored_order != NULL);
    uint16_t queued[3] = {0};
    CHECK_EQUAL(3, player.storage.order.count);
    CHECK(track_order_read(&player.storage.order, 0, 3, queued));
    CHECK_EQUAL(4, queued[0]);
    CHECK_EQUAL(0, queued[1]);
    CHECK_EQUAL(2, queued[2]);
    int plays = fake_hal_audio_play_count();
    action = key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK(action.target == &now_playing);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static void a_failed_queue_write_keeps_the_list_and_says_so(void)
{
    start();
    add_play(2);
    show(SMART_LIST_RECENTLY_PLAYED);
    fake_hal_fail_file_writes(PLAYER_ORDER_PATH);
    ScreenAction action = key(KEY_PLAY, KEY_GESTURE_PRESS);
    fake_hal_fail_file_writes(NULL);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK(strcmp(toast.text, i18n_text(STR_QUEUE_SAVE_FAILED)) == 0);
    CHECK(!player_status(&player).has_track);
    CHECK_EQUAL(1, rows());
}

static void holding_play_offers_play_next_and_add_to_up_next(void)
{
    start();
    add_play(3);
    show(SMART_LIST_RECENTLY_PLAYED);
    ScreenAction action = key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &pool.song_actions.menu.screen);
    CHECK_EQUAL(3, pool.song_actions.track);
    CHECK(strcmp(pool.song_actions.items[0].label, i18n_text(STR_PLAY_NEXT)) == 0);
    CHECK(strcmp(pool.song_actions.items[1].label, i18n_text(STR_ADD_TO_UP_NEXT)) == 0);
    CHECK(strcmp(pool.song_actions.title, smart.model.label(&smart, 0)) == 0);
}

static void leaving_while_working_stops_the_job_and_a_later_open_starts_again(void)
{
    start();
    open_list(SMART_LIST_NEVER_PLAYED);
    CHECK(working());
    CHECK(smart.checking);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    open_list(SMART_LIST_RECENTLY_ADDED);
    CHECK(working());
    CHECK(smart.checking);
    finish();
    CHECK_EQUAL(0, rows());
    open_list(SMART_LIST_NEVER_PLAYED);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, key(KEY_BACK, KEY_GESTURE_LONG_PRESS).type);
    open_list(SMART_LIST_NEVER_PLAYED);
    finish();
    CHECK_EQUAL((int)library_music_count(&fixture.library, LIBRARY_TRACKS), rows());
}

static void without_the_track_keys_file_the_history_is_unavailable(void)
{
    start_with(false);
    add_play(1);
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(0, rows());
    draw();
    CHECK(saw(i18n_text(STR_HISTORY_UNAVAILABLE)));
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void the_menu_names_every_list(void)
{
    static const StringId NAMES[SMART_LIST_KIND_COUNT] = {STR_RECENTLY_ADDED, STR_RECENTLY_PLAYED,
                                                          STR_MOST_PLAYED, STR_NEVER_PLAYED,
                                                          STR_FORGOTTEN_FAVORITES};
    start();
    CHECK(strcmp(smart.menu.screen.title, i18n_text(STR_SMART_LISTS)) == 0);
    for (int kind = 0; kind < SMART_LIST_KIND_COUNT; kind++) {
        CHECK(strcmp(smart.items[kind].label, i18n_text(NAMES[kind])) == 0);
        CHECK(smart.items[kind].opens);
    }
}

static void lists_render_at_every_size_and_orientation(void)
{
    for (int variant = 0; variant < 2 * TEXT_SIZE_COUNT; variant++) {
        start();
        default_theme_set_text_size(&theme, (TextSize)(variant % TEXT_SIZE_COUNT));
        default_theme_set_orientation(&theme, (Orientation)(variant / TEXT_SIZE_COUNT));
        for (uint16_t track = 0; track < 6; track++) {
            add_play(track);
        }
        show(SMART_LIST_RECENTLY_PLAYED);
        Canvas canvas = draw();
        CHECK(rows() == 6);
        CHECK(saw(smart.model.label(&smart, 0)));
        screen_snapshot_write(&canvas, "smart-recently-played", variant);
        show(SMART_LIST_RECENTLY_ADDED);
        canvas = draw();
        CHECK(saw(i18n_text(STR_SMART_NONE_ADDED)));
        screen_snapshot_write(&canvas, "smart-recently-added-empty", variant);
    }
}

static void clearing_history_rebuilds_cached_most_and_recently_played_lists(void)
{
    start();
    add_play(0);
    add_play(4);
    show(SMART_LIST_MOST_PLAYED);
    CHECK_EQUAL(2, rows());
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(2, rows());
    uint32_t generation = store.log.generation;
    CHECK_EQUAL(STORE_DONE, listening_store_clear(&store));
    CHECK(store.log.generation != generation);
    show(SMART_LIST_MOST_PLAYED);
    CHECK_EQUAL(0, rows());
    CHECK_EQUAL(store.log.generation, smart.caches[SMART_LIST_MOST_PLAYED].stamp.log);
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(0, rows());
    CHECK_EQUAL(store.log.generation, smart.caches[SMART_LIST_RECENTLY_PLAYED].stamp.log);
}

static void missing_dependencies_and_stale_rows_fail_closed(void)
{
    start();
    pool.config.favorites = NULL;
    show(SMART_LIST_FORGOTTEN);
    CHECK_EQUAL(0, rows());

    start();
    add_play(2);
    show(SMART_LIST_RECENTLY_PLAYED);
    CHECK_EQUAL(1, rows());
    smart.caches[SMART_LIST_RECENTLY_PLAYED].tracks[0] = UINT16_MAX;
    listening_module_work.owner = NULL;
    tick();
    CHECK_EQUAL(0, smart.model.label(&smart, 0)[0]);

    smart.caches[SMART_LIST_RECENTLY_PLAYED].tracks[0] = 2;
    listening_module_work.owner = NULL;
    tick();
    pool.config.player = NULL;
    ScreenAction action = key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK(strcmp(toast.text, i18n_text(STR_QUEUE_SAVE_FAILED)) == 0);
    action = key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(clearing_history_rebuilds_cached_most_and_recently_played_lists),
        TEST_CASE(recently_played_shows_songs_newest_first_with_their_artist),
        TEST_CASE(each_list_has_its_own_empty_state),
        TEST_CASE(the_first_list_checks_the_library_and_says_so),
        TEST_CASE(a_list_is_kept_until_a_play_is_logged),
        TEST_CASE(a_favorite_change_refreshes_old_favorites),
        TEST_CASE(old_favorites_lists_long_unplayed_favorites_first),
        TEST_CASE(songs_new_after_a_library_change_are_recently_added),
        TEST_CASE(the_parts_of_a_cue_file_are_separate_songs),
        TEST_CASE(playing_a_list_queues_its_exact_order_and_opens_now_playing),
        TEST_CASE(a_failed_queue_write_keeps_the_list_and_says_so),
        TEST_CASE(holding_play_offers_play_next_and_add_to_up_next),
        TEST_CASE(leaving_while_working_stops_the_job_and_a_later_open_starts_again),
        TEST_CASE(without_the_track_keys_file_the_history_is_unavailable),
        TEST_CASE(the_menu_names_every_list),
        TEST_CASE(lists_render_at_every_size_and_orientation),
        TEST_CASE(missing_dependencies_and_stale_rows_fail_closed),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
