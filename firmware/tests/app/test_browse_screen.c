#include <string.h>

#include "browse_screen.h"
#include "browse_sections.h"
#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fixture_library.h"

static FixtureLibrary fixture;
static BrowsePool pool;

static BrowseScreen *opened(BrowseQuery query)
{
    browse_pool_init(&pool, (BrowsePoolConfig){.library = fixture_library_open(&fixture),
                                               .theme = &BOARD_THEME,
                                               .section = browse_sections_at,
                                               .sections = browse_sections_range});
    Screen *screen = browse_pool_open(&pool, 0, query);
    screen->enter(screen->context, 0);
    return screen->context;
}

static int section_at(const BrowseScreen *browse, int index)
{
    return browse->model.section(browse->model.context, index);
}

static void albums_are_lettered_by_their_sort_key(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    CHECK(albums->model.section != NULL);
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'b' - 'a', section_at(albums, 0));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'b' - 'a', section_at(albums, 1));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'h' - 'a', section_at(albums, 2));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 't' - 'a', section_at(albums, 4));
}

static void lettered_lists_never_go_backwards(void)
{
    static const BrowseKind LETTERED[] = {BROWSE_ARTISTS, BROWSE_ALBUM_ARTISTS, BROWSE_GENRES,
                                          BROWSE_ALBUMS, BROWSE_ALL_SONGS};
    for (size_t kind = 0; kind < sizeof LETTERED / sizeof LETTERED[0]; kind++) {
        BrowseScreen *browse = opened(browse_root_query(LETTERED[kind]));
        CHECK(browse->model.section != NULL);
        for (int index = 1; index < (int)browse->count; index++) {
            CHECK(section_at(browse, index - 1) <= section_at(browse, index));
        }
    }
}

static void album_tracks_have_no_letters(void)
{
    BrowseQuery tracks = {.kind = BROWSE_ALBUM_TRACKS,
                          .group = LIBRARY_ARTISTS,
                          .subject = 0,
                          .artist = LIBRARY_NONE};
    CHECK_EQUAL(0, browse_sections_range(opened(tracks), 0).end);
}

static void folder_names_offer_sections_without_reordering_tracks(void)
{
    BrowseScreen *folders = opened(browse_root_query(BROWSE_FOLDER));
    ListSections sections = browse_sections_range(folders, 0);
    CHECK_EQUAL(0, sections.first);
    CHECK(sections.end > 0);
    for (int index = 1; index < sections.end; index++) {
        CHECK(section_at(folders, index - 1) <= section_at(folders, index));
    }
}

static void an_appended_favorite_playlist_has_its_own_sorted_run(void)
{
    BrowseScreen *playlists = opened(browse_root_query(BROWSE_PLAYLISTS));
    uint32_t stored = playlists->count;
    LibraryExtraPlaylist extra = {.name = "A favorite", .playlist = (uint16_t)stored, .count = 1};
    fixture.library.extra = &extra;
    playlists->count = browse_count(&fixture.library, playlists->query);
    CHECK_EQUAL(stored + 1, playlists->count);
    CHECK_EQUAL(stored, browse_sections_range(playlists, 0).end);
    CHECK_EQUAL(stored, browse_sections_range(playlists, (int)stored).first);
}

static void numbered_files_do_not_become_single_item_letter_runs(void)
{
    BrowseQuery query = browse_root_query(BROWSE_FOLDER);
    query.subject = 2;
    BrowseScreen *files = opened(query);
    CHECK_EQUAL(0, browse_sections_range(files, 0).end);
}

static void name_ordered_files_are_one_letter_run(void)
{
    BrowseQuery query = browse_root_query(BROWSE_FOLDER);
    query.subject = 2;
    BrowseScreen *files = opened(query);
    LibrarySectionInfo folders = fixture.library.sections[LIBRARY_FOLDERS];
    fixture.bytes[folders.offset + 2 * folders.record_size + 14] = LIBRARY_FOLDER_NAMED;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&fixture.library, memory_source(&fixture.memory), fixture.size));
    LibraryFolder folder;
    CHECK(library_read_folder(&fixture.library, 2, &folder));
    CHECK(folder.track_count > 1);
    int last = folder.subfolder_count + folder.track_count - 1;
    ListSections sections = browse_sections_range(files, last);
    CHECK_EQUAL(folder.subfolder_count, sections.first);
    CHECK_EQUAL(last + 1, sections.end);
}

static void section_ranges_cover_each_root_list_contract(void)
{
    static const BrowseKind WHOLE_LIST[] = {BROWSE_ARTISTS, BROWSE_ALBUM_ARTISTS, BROWSE_ALBUMS,
                                            BROWSE_ALL_SONGS, BROWSE_GENRES};
    for (size_t index = 0; index < sizeof WHOLE_LIST / sizeof WHOLE_LIST[0]; index++) {
        BrowseScreen *browse = opened(browse_root_query(WHOLE_LIST[index]));
        ListSections sections = browse_sections_range(browse, (int)browse->count);
        CHECK_EQUAL(0, sections.first);
        CHECK_EQUAL(browse->count, sections.end);
    }

    BrowseQuery grouped = {.kind = BROWSE_GROUP_ALBUMS, .group = LIBRARY_GENRES};
    BrowseScreen *genres = opened(grouped);
    CHECK_EQUAL(genres->count, browse_sections_range(genres, 0).end);
    grouped.group = LIBRARY_ARTISTS;
    CHECK_EQUAL(0, browse_sections_range(opened(grouped), 0).end);

    BrowseScreen *favorites = opened(browse_root_query(BROWSE_ALBUMS));
    favorites->favorites_only = true;
    CHECK_EQUAL(favorites->count, browse_sections_range(favorites, 0).end);
}

static void missing_and_spoken_folders_have_no_false_ranges(void)
{
    BrowseQuery missing = browse_root_query(BROWSE_FOLDER);
    missing.subject = LIBRARY_NONE;
    CHECK_EQUAL(0, browse_sections_range(opened(missing), 0).end);

    BrowseScreen *folders = opened(browse_root_query(BROWSE_FOLDER));
    CHECK_EQUAL(0, browse_sections_range(folders, 100000).end);
    folders->shelf_root = true;
    folders->shelf = SPOKEN_AUDIOBOOK;
    CHECK_EQUAL(0, browse_sections_range(folders, 100000).end);
}

static void invalid_levels_and_missing_favorites_return_no_screen(void)
{
    const Library *library = fixture_library_open(&fixture);
    BrowsePoolConfig config = {.library = library, .theme = &BOARD_THEME};
    browse_pool_init(&pool, config);
    BrowseQuery query = browse_root_query(BROWSE_ALBUMS);
    CHECK(browse_pool_open(&pool, -1, query) == NULL);
    CHECK(browse_pool_open(&pool, BROWSE_DEPTH, query) == NULL);
    CHECK(browse_pool_open_favorites(&pool, 0) == NULL);

    FavoritesStore favorites = {.library = library};
    config.favorites = &favorites;
    browse_pool_init(&pool, config);
    CHECK(browse_pool_open_favorites(&pool, -1) == NULL);
    CHECK(browse_pool_open_shelf(&pool, BROWSE_DEPTH, SPOKEN_AUDIOBOOK) == NULL);
}

static int song_action_row(StringId label)
{
    for (int index = 0; index < pool.song_actions.menu.config.item_count; index++) {
        if (strcmp(pool.song_actions.items[index].label, i18n_text(label)) == 0) {
            return index;
        }
    }
    return -1;
}

static Screen *manage_nothing(void *context, uint16_t track, SongManage manage)
{
    (void)context;
    (void)track;
    (void)manage;
    return NULL;
}

static void the_favourite_playlist_row_needs_a_manage_handler(void)
{
    BrowsePoolConfig config = {.library = fixture_library_open(&fixture), .theme = &BOARD_THEME};
    browse_pool_init(&pool, config);
    CHECK(browse_pool_song_actions(&pool, 0, "Song", 0) != NULL);
    CHECK(song_action_row(STR_ADD_TO_UP_NEXT) >= 0);
    CHECK_EQUAL(-1, song_action_row(STR_ADD_TO_FAVORITE_PLAYLIST));
    config.manage = manage_nothing;
    browse_pool_init(&pool, config);
    CHECK(browse_pool_song_actions(&pool, 0, "Song", 0) != NULL);
    int row = song_action_row(STR_ADD_TO_FAVORITE_PLAYLIST);
    CHECK(row >= 0);
    pool.config.manage = NULL;
    MenuItem item = pool.song_actions.items[row];
    CHECK_EQUAL(SCREEN_ACTION_POP, item.select(item.context).type);
}

static void every_track_action_has_an_icon_and_the_file_rows_say_what_happens(void)
{
    BrowsePoolConfig config = {
        .library = fixture_library_open(&fixture), .theme = &BOARD_THEME, .manage = manage_nothing};
    browse_pool_init(&pool, config);
    CHECK(browse_pool_song_actions(&pool, 0, "Song", 0) != NULL);
    int count = pool.song_actions.menu.config.item_count;
    for (int index = 0; index < count; index++) {
        CHECK(pool.song_actions.items[index].icon != ICON_NONE);
    }
    MenuItem hide = pool.song_actions.items[song_action_row(STR_HIDE_TRACK)];
    MenuItem erase = pool.song_actions.items[count - 1];
    CHECK(strcmp(i18n_text(STR_KEEP_FILE), hide.value(hide.context)) == 0);
    CHECK(strcmp(i18n_text(STR_DELETE_FILE_ASK), erase.label) == 0);
    CHECK(strcmp(i18n_text(STR_ERASE_FROM_STORAGE), erase.value(erase.context)) == 0);
    CHECK_EQUAL(LIST_ROW_DANGER_DIVIDED, erase.role);
    CHECK_EQUAL(LIST_ROW_PLAIN, hide.role);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(albums_are_lettered_by_their_sort_key),
        TEST_CASE(folder_names_offer_sections_without_reordering_tracks),
        TEST_CASE(an_appended_favorite_playlist_has_its_own_sorted_run),
        TEST_CASE(numbered_files_do_not_become_single_item_letter_runs),
        TEST_CASE(name_ordered_files_are_one_letter_run),
        TEST_CASE(section_ranges_cover_each_root_list_contract),
        TEST_CASE(missing_and_spoken_folders_have_no_false_ranges),
        TEST_CASE(lettered_lists_never_go_backwards),
        TEST_CASE(album_tracks_have_no_letters),
        TEST_CASE(invalid_levels_and_missing_favorites_return_no_screen),
        TEST_CASE(the_favourite_playlist_row_needs_a_manage_handler),
        TEST_CASE(every_track_action_has_an_icon_and_the_file_rows_say_what_happens),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
