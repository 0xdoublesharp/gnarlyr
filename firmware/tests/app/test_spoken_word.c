#include <string.h>

#include "browse_screen.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "library/library_browse.h"
#include "player.h"
#include "spoken_library.h"
#include "spoken_store.h"

enum {
    SEED = 11,
    MIDWAY_MS = 123000,
    NEAR_END_MS = SPOKEN_EPISODE_MS - 5000
};

static SpokenLibrary spoken;
static Library *library;
static Player player;
static SpokenStore store;
static BrowsePool pool;
static Screen now_playing = {.title = "Now Playing"};

static bool path_is(const char *actual, const char *expected)
{
    return actual != NULL && actual[0] == '/' && strcmp(expected, actual + 1) == 0;
}

static void start(void)
{
    fake_hal_reset();
    player_init(&player, library);
    spoken_store_load(&store, library);
    player_set_resume(&player, spoken_store_resume(&store));
}

static BrowseQuery folder_of(const char *path, uint32_t *position)
{
    uint16_t track = spoken_library_track(library, path);
    LibraryTrack record;
    uint16_t chapter = 0;
    BrowseQuery query = browse_root_query(BROWSE_FOLDER);
    library_read_track(library, track, &record);
    spoken_chapter_of(library, track, &chapter);
    browse_child(query, (BrowseItem){.kind = BROWSE_ITEM_FOLDER, .index = record.folder}, &query);
    *position = chapter;
    return query;
}

static void play(const char *path, bool in_order)
{
    uint32_t position = 0;
    BrowseQuery source = folder_of(path, &position);
    PlayerListStart list = {
        .source = source, .position = position, .seed = SEED, .in_order = in_order};
    CHECK(player_play_from(&player, list));
}

static void follow(uint32_t now_ms)
{
    spoken_store_follow(&store, player_status(&player), now_ms);
}

static void books_play_in_order_even_with_shuffle_on(void)
{
    start();
    player_set_shuffle(&player, SHUFFLE_SONGS, SEED);
    play(SPOKEN_CHAPTER_ONE_PATH, true);
    CHECK(player_status(&player).spoken);
    CHECK(path_is(fake_hal_audio_queued_path(), SPOKEN_CHAPTER_TWO_PATH));
    player_set_shuffle(&player, SHUFFLE_SONGS, SEED + 1);
    CHECK(path_is(fake_hal_audio_queued_path(), SPOKEN_CHAPTER_TWO_PATH));
}

static void holding_volume_jumps_within_a_chapter(void)
{
    start();
    play(SPOKEN_CHAPTER_ONE_PATH, true);
    fake_hal_audio_set_position(100000);
    player_skip(&player, 1);
    CHECK_EQUAL(100000 + SPOKEN_JUMP_FORWARD_MS, fake_hal_audio_last_seek());
    fake_hal_audio_set_position(100000);
    player_skip(&player, -1);
    CHECK_EQUAL(100000 - SPOKEN_JUMP_BACK_MS, fake_hal_audio_last_seek());
    fake_hal_audio_set_position(5000);
    player_skip(&player, -1);
    CHECK_EQUAL(0, fake_hal_audio_last_seek());
}

static void jumping_past_the_end_starts_the_next_chapter(void)
{
    start();
    play(SPOKEN_CHAPTER_ONE_PATH, true);
    fake_hal_audio_set_position(SPOKEN_CHAPTER_MS - 10000);
    player_skip(&player, 1);
    CHECK(path_is(fake_hal_audio_playing_path(), SPOKEN_CHAPTER_TWO_PATH));
}

static void music_still_skips_tracks(void)
{
    start();
    play(SPOKEN_SONG_PATH, false);
    CHECK(!player_status(&player).spoken);
    fake_hal_audio_set_position(100000);
    player_skip(&player, -1);
    CHECK_EQUAL(0, fake_hal_audio_last_seek());
}

static void a_chapter_resumes_where_it_stopped(void)
{
    start();
    play(SPOKEN_CHAPTER_TWO_PATH, true);
    follow(0);
    fake_hal_audio_set_position(MIDWAY_MS);
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    play(SPOKEN_CHAPTER_TWO_PATH, true);
    CHECK_EQUAL(MIDWAY_MS, fake_hal_audio_play_start());
}

static void the_bookmark_is_saved_to_the_card(void)
{
    start();
    play(SPOKEN_CHAPTER_TWO_PATH, true);
    follow(0);
    fake_hal_audio_set_position(MIDWAY_MS);
    player_toggle_pause(&player);
    follow(1000);
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(SPOKEN_STORE_PATH, &size);
    Bookmarks saved;
    Bookmark found;
    CHECK(bytes != NULL && bookmarks_decode(bytes, size, &saved));
    CHECK(bookmarks_find(&saved, spoken_bookmark_key(SPOKEN_CHAPTER_TWO_PATH), &found));
    CHECK_EQUAL(MIDWAY_MS, found.position_ms);
    CHECK_EQUAL(1, found.chapter);
}

static void a_finished_episode_is_marked_played(void)
{
    start();
    play(SPOKEN_EPISODE_A_PATH, true);
    follow(0);
    fake_hal_audio_set_position(NEAR_END_MS);
    follow(1000);
    fake_hal_audio_finish_track();
    player_update(&player, 2000);
    follow(2000);
    Bookmark found;
    CHECK(spoken_store_track(&store, spoken_library_track(library, SPOKEN_EPISODE_A_PATH), &found));
    CHECK(bookmark_finished(found));
}

static void a_played_episode_starts_from_the_top(void)
{
    start();
    play(SPOKEN_EPISODE_A_PATH, true);
    follow(0);
    fake_hal_audio_set_position(NEAR_END_MS);
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    play(SPOKEN_EPISODE_A_PATH, true);
    CHECK_EQUAL(0, fake_hal_audio_play_start());
}

static BrowseScreen *shelf(SpokenKind kind)
{
    browse_pool_init(&pool, (BrowsePoolConfig){.library = library,
                                               .theme = &BOARD_THEME,
                                               .player = &player,
                                               .now_playing = &now_playing,
                                               .spoken = &store});
    Screen *screen = browse_pool_open_shelf(&pool, 0, kind);
    screen->enter(screen->context, 0);
    return screen->context;
}

static const char *detail_of(BrowseScreen *browse, int index)
{
    const char *detail = browse->model.detail(browse->model.context, index);
    return detail != NULL ? detail : "";
}

static ScreenAction press(BrowseScreen *browse, KeyGesture gesture)
{
    KeyEvent event = {.key = KEY_PLAY, .gesture = gesture};
    return browse->screen.handle_key(browse, event, 0);
}

static uint32_t music_count(BrowseKind kind)
{
    return browse_count(library, (BrowseQuery){.kind = kind});
}

static void the_music_lists_leave_out_spoken_word(void)
{
    start();
    CHECK_EQUAL(10, library_count(library, LIBRARY_TRACKS));
    CHECK_EQUAL(1, library_music_count(library, LIBRARY_TRACKS));
    CHECK_EQUAL(1, music_count(BROWSE_ALBUMS));
    CHECK_EQUAL(1, music_count(BROWSE_ALL_SONGS));
    CHECK_EQUAL(1, music_count(BROWSE_ARTISTS));
    CHECK_EQUAL(1, music_count(BROWSE_ALBUM_ARTISTS));
    CHECK_EQUAL(1, music_count(BROWSE_GENRES));
    CHECK_EQUAL(spoken_library_track(library, SPOKEN_SONG_PATH), 0);
}

static void the_audiobook_shelf_lists_books(void)
{
    start();
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    CHECK_EQUAL(3, books->count);
    CHECK(strcmp("Audiobooks", books->title) == 0);
    CHECK(strcmp("3 chapters", detail_of(books, 0)) == 0);
}

static void a_started_book_shows_its_chapter(void)
{
    start();
    play(SPOKEN_CHAPTER_TWO_PATH, true);
    follow(0);
    fake_hal_audio_set_position(MIDWAY_MS);
    follow(1000);
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    CHECK(strcmp("Chapter 2 of 3", detail_of(books, 0)) == 0);
}

static void selecting_a_book_resumes_it(void)
{
    start();
    play(SPOKEN_CHAPTER_TWO_PATH, true);
    follow(0);
    fake_hal_audio_set_position(MIDWAY_MS);
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    ScreenAction action = press(books, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &now_playing);
    CHECK(path_is(fake_hal_audio_playing_path(), SPOKEN_CHAPTER_TWO_PATH));
    CHECK_EQUAL(MIDWAY_MS, fake_hal_audio_play_start());
}

static void selecting_a_paused_book_carries_on_playing_it(void)
{
    start();
    play(SPOKEN_CHAPTER_TWO_PATH, true);
    player_toggle_pause(&player);
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    ScreenAction action = press(books, KEY_GESTURE_PRESS);
    CHECK(action.target == &now_playing);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
    CHECK_EQUAL(1, fake_hal_audio_play_count());
}

static void holding_play_on_a_book_opens_its_chapters(void)
{
    start();
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    ScreenAction action = press(books, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    BrowseScreen *chapters = action.target->context;
    CHECK_EQUAL(3, chapters->count);
    CHECK_EQUAL(SPOKEN_AUDIOBOOK, chapters->shelf);
}

static void a_played_episode_says_so_in_its_show(void)
{
    start();
    play(SPOKEN_EPISODE_A_PATH, true);
    follow(0);
    fake_hal_audio_set_position(NEAR_END_MS);
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    BrowseScreen *shows = shelf(SPOKEN_PODCAST);
    CHECK(strcmp("2 episodes", detail_of(shows, 0)) == 0);
    ScreenAction action = press(shows, KEY_GESTURE_PRESS);
    BrowseScreen *episodes = action.target->context;
    episodes->screen.enter(episodes, 0);
    CHECK(strcmp("Played", detail_of(episodes, 0)) == 0);
}

static BrowseQuery parts_query(void)
{
    BrowseQuery query = browse_root_query(BROWSE_FOLDER);
    uint16_t books = 0;
    BrowseItem item;
    CHECK(spoken_shelf_folder(library, SPOKEN_AUDIOBOOK, &books));
    browse_child(query, (BrowseItem){.kind = BROWSE_ITEM_FOLDER, .index = books}, &query);
    CHECK(browse_item(library, query, 1, &item));
    CHECK_EQUAL(BROWSE_ITEM_FOLDER, item.kind);
    browse_child(query, item, &query);
    return query;
}

static void play_part(uint32_t chapter)
{
    PlayerListStart list = {
        .source = parts_query(), .position = chapter, .seed = SEED, .in_order = true};
    CHECK(player_play_from(&player, list));
}

static uint32_t at_part(uint32_t chapter_start, uint32_t position_ms)
{
    return chapter_start + position_ms;
}

static void a_chaptered_file_is_a_book_of_its_own_on_the_shelf(void)
{
    start();
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    CHECK(strcmp("3 chapters", detail_of(books, 1)) == 0);
    ScreenAction action =
        (list_view_select(&books->list, 1, 0), press(books, KEY_GESTURE_LONG_PRESS));
    BrowseScreen *chapters = action.target->context;
    chapters->screen.enter(chapters, 0);
    CHECK_EQUAL(3, chapters->count);
    CHECK(strcmp("Noon", chapters->model.label(chapters->model.context, 1)) == 0);
}

static BrowseScreen *nested_book_folder(void)
{
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    list_view_select(&books->list, 1, 0);
    ScreenAction author = press(books, KEY_GESTURE_PRESS);
    BrowseScreen *writer = author.target->context;
    writer->screen.enter(writer, 0);
    ScreenAction title = press(writer, KEY_GESTURE_PRESS);
    BrowseScreen *folder = title.target->context;
    folder->screen.enter(folder, 0);
    return folder;
}

static void start_nested(void)
{
    spoken_library_close(&spoken);
    library = spoken_library_open_nested(&spoken);
    start();
}

static void restore_library(void)
{
    spoken_library_close(&spoken);
    library = spoken_library_open(&spoken);
}

static void a_nested_chaptered_book_starts_when_selected(void)
{
    start_nested();
    BrowseScreen *book = nested_book_folder();
    CHECK_EQUAL(1, book->count);
    ScreenAction action = press(book, KEY_GESTURE_PRESS);
    CHECK(action.target == &now_playing);
    CHECK(path_is(fake_hal_audio_playing_path(), SPOKEN_NESTED_PARTS_PATH));
    restore_library();
}

static BrowseScreen *nested_chapters(void)
{
    BrowseScreen *book = nested_book_folder();
    ScreenAction action = press(book, KEY_GESTURE_LONG_PRESS);
    BrowseScreen *parts = action.target->context;
    parts->screen.enter(parts, 0);
    return parts;
}

static void play_nested_part(int part)
{
    BrowseScreen *parts = nested_chapters();
    list_view_select(&parts->list, part, 0);
    CHECK(press(parts, KEY_GESTURE_PRESS).target == &now_playing);
}

static void a_nested_book_resumes_in_its_saved_chapter_and_position(void)
{
    start_nested();
    play_nested_part(1);
    follow(0);
    fake_hal_audio_set_position(at_part(SPOKEN_PART_ONE_MS, MIDWAY_MS));
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    BrowseScreen *book = nested_book_folder();
    CHECK(strcmp("Chapter 2 of 3", detail_of(book, 0)) == 0);
    CHECK(press(book, KEY_GESTURE_PRESS).target == &now_playing);
    CHECK(path_is(fake_hal_audio_playing_path(), SPOKEN_NESTED_PARTS_PATH));
    CHECK_EQUAL(at_part(SPOKEN_PART_ONE_MS, MIDWAY_MS), fake_hal_audio_play_start());
    LibraryTrack record;
    CHECK(library_read_track(library, player_status(&player).track, &record));
    CHECK_EQUAL(2, record.number);
    restore_library();
}

static void selecting_a_paused_nested_book_keeps_the_loaded_stream(void)
{
    start_nested();
    play_nested_part(2);
    player_toggle_pause(&player);
    BrowseScreen *book = nested_book_folder();
    CHECK(press(book, KEY_GESTURE_PRESS).target == &now_playing);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
    CHECK_EQUAL(1, fake_hal_audio_play_count());
    restore_library();
}

static void holding_play_on_a_nested_book_opens_chapters_without_starting_audio(void)
{
    start_nested();
    BrowseScreen *parts = nested_chapters();
    CHECK_EQUAL(3, parts->count);
    CHECK_EQUAL(SPOKEN_AUDIOBOOK, parts->shelf);
    CHECK(strcmp("Noon", parts->model.label(parts->model.context, 1)) == 0);
    CHECK_EQUAL(0, fake_hal_audio_play_count());
    restore_library();
}

static BrowseScreen *nested_multi_file_book(void)
{
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    ScreenAction author = press(books, KEY_GESTURE_PRESS);
    BrowseScreen *folder = author.target->context;
    folder->screen.enter(folder, 0);
    return folder;
}

static void a_nested_multi_file_book_resumes_its_saved_track(void)
{
    start_nested();
    BrowseScreen *author = nested_multi_file_book();
    ScreenAction chapters = press(author, KEY_GESTURE_LONG_PRESS);
    BrowseScreen *parts = chapters.target->context;
    parts->screen.enter(parts, 0);
    list_view_select(&parts->list, 1, 0);
    CHECK(press(parts, KEY_GESTURE_PRESS).target == &now_playing);
    follow(0);
    fake_hal_audio_set_position(MIDWAY_MS);
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    author = nested_multi_file_book();
    CHECK(strcmp("Chapter 2 of 3", detail_of(author, 0)) == 0);
    CHECK(press(author, KEY_GESTURE_PRESS).target == &now_playing);
    CHECK(path_is(fake_hal_audio_playing_path(), SPOKEN_NESTED_CHAPTER_TWO_PATH));
    CHECK_EQUAL(MIDWAY_MS, fake_hal_audio_play_start());
    restore_library();
}

static void a_chaptered_file_plays_its_chapters_in_order(void)
{
    start();
    play_part(0);
    CHECK(path_is(fake_hal_audio_playing_path(), SPOKEN_PARTS_PATH));
    CHECK_EQUAL(spoken_library_chapter(library, 1), player_status(&player).track);
    fake_hal_audio_set_position(SPOKEN_PART_ONE_MS - 10000);
    player_skip(&player, 1);
    CHECK_EQUAL(spoken_library_chapter(library, 2), player_status(&player).track);
    CHECK_EQUAL(SPOKEN_PART_ONE_MS, fake_hal_audio_play_start());
}

static void holding_volume_jumps_within_a_chapter_of_a_file(void)
{
    start();
    play_part(1);
    fake_hal_audio_set_position(at_part(SPOKEN_PART_ONE_MS, 100000));
    player_skip(&player, 1);
    CHECK_EQUAL(at_part(SPOKEN_PART_ONE_MS, 100000 + SPOKEN_JUMP_FORWARD_MS),
                fake_hal_audio_last_seek());
    fake_hal_audio_set_position(at_part(SPOKEN_PART_ONE_MS, 5000));
    player_skip(&player, -1);
    CHECK_EQUAL(SPOKEN_PART_ONE_MS, fake_hal_audio_last_seek());
}

static void a_chaptered_book_resumes_in_its_chapter_and_position(void)
{
    start();
    play_part(1);
    follow(0);
    fake_hal_audio_set_position(at_part(SPOKEN_PART_ONE_MS, MIDWAY_MS));
    follow(1000);
    play(SPOKEN_SONG_PATH, false);
    follow(2000);
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    CHECK(strcmp("Chapter 2 of 3", detail_of(books, 1)) == 0);
    list_view_select(&books->list, 1, 0);
    ScreenAction action = press(books, KEY_GESTURE_PRESS);
    CHECK(action.target == &now_playing);
    CHECK_EQUAL(spoken_library_chapter(library, 2), player_status(&player).track);
    CHECK_EQUAL(at_part(SPOKEN_PART_ONE_MS, MIDWAY_MS), fake_hal_audio_play_start());
}

static void a_loaded_chaptered_book_is_not_restarted(void)
{
    start();
    play_part(2);
    player_toggle_pause(&player);
    BrowseScreen *books = shelf(SPOKEN_AUDIOBOOK);
    list_view_select(&books->list, 1, 0);
    ScreenAction action = press(books, KEY_GESTURE_PRESS);
    CHECK(action.target == &now_playing);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
    CHECK_EQUAL(1, fake_hal_audio_play_count());
}

static void a_chaptered_book_is_bookmarked_under_its_file_path(void)
{
    start();
    play_part(1);
    follow(0);
    fake_hal_audio_set_position(at_part(SPOKEN_PART_ONE_MS, MIDWAY_MS));
    player_toggle_pause(&player);
    follow(1000);
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(SPOKEN_STORE_PATH, &size);
    Bookmarks saved;
    Bookmark found;
    CHECK(bytes != NULL && bookmarks_decode(bytes, size, &saved));
    CHECK(bookmarks_find(&saved, spoken_track_key(SPOKEN_PARTS_PATH), &found));
    CHECK_EQUAL(MIDWAY_MS, found.position_ms);
    CHECK_EQUAL(1, found.chapter);
    CHECK_EQUAL(spoken_part_key(SPOKEN_PARTS_PATH, SPOKEN_PART_ONE_MS), found.track_key);
}

static void another_chapter_of_the_file_starts_from_its_top(void)
{
    start();
    play_part(1);
    follow(0);
    fake_hal_audio_set_position(at_part(SPOKEN_PART_ONE_MS, MIDWAY_MS));
    follow(1000);
    Bookmark found;
    CHECK(spoken_store_track(&store, spoken_library_chapter(library, 2), &found));
    CHECK(!spoken_store_track(&store, spoken_library_chapter(library, 1), &found));
    CHECK(!spoken_store_track(&store, spoken_library_chapter(library, 3), &found));
    play_part(2);
    CHECK_EQUAL(SPOKEN_PART_ONE_MS + SPOKEN_PART_TWO_MS, fake_hal_audio_play_start());
}

int main(void)
{
    library = spoken_library_open(&spoken);
    static const TestCase cases[] = {
        TEST_CASE(books_play_in_order_even_with_shuffle_on),
        TEST_CASE(holding_volume_jumps_within_a_chapter),
        TEST_CASE(jumping_past_the_end_starts_the_next_chapter),
        TEST_CASE(music_still_skips_tracks),
        TEST_CASE(a_chapter_resumes_where_it_stopped),
        TEST_CASE(the_bookmark_is_saved_to_the_card),
        TEST_CASE(a_finished_episode_is_marked_played),
        TEST_CASE(a_played_episode_starts_from_the_top),
        TEST_CASE(the_music_lists_leave_out_spoken_word),
        TEST_CASE(the_audiobook_shelf_lists_books),
        TEST_CASE(a_started_book_shows_its_chapter),
        TEST_CASE(selecting_a_book_resumes_it),
        TEST_CASE(selecting_a_paused_book_carries_on_playing_it),
        TEST_CASE(holding_play_on_a_book_opens_its_chapters),
        TEST_CASE(a_played_episode_says_so_in_its_show),
        TEST_CASE(a_chaptered_file_is_a_book_of_its_own_on_the_shelf),
        TEST_CASE(a_nested_chaptered_book_starts_when_selected),
        TEST_CASE(a_nested_multi_file_book_resumes_its_saved_track),
        TEST_CASE(a_nested_book_resumes_in_its_saved_chapter_and_position),
        TEST_CASE(selecting_a_paused_nested_book_keeps_the_loaded_stream),
        TEST_CASE(holding_play_on_a_nested_book_opens_chapters_without_starting_audio),
        TEST_CASE(a_chaptered_file_plays_its_chapters_in_order),
        TEST_CASE(holding_volume_jumps_within_a_chapter_of_a_file),
        TEST_CASE(a_chaptered_book_resumes_in_its_chapter_and_position),
        TEST_CASE(a_loaded_chaptered_book_is_not_restarted),
        TEST_CASE(a_chaptered_book_is_bookmarked_under_its_file_path),
        TEST_CASE(another_chapter_of_the_file_starts_from_its_top),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    spoken_library_close(&spoken);
    return result;
}
