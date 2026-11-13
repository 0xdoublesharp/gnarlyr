#include "browse_screen.h"
#include "browse_sections.h"
#include "check.h"
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

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(albums_are_lettered_by_their_sort_key),
        TEST_CASE(folder_names_offer_sections_without_reordering_tracks),
        TEST_CASE(an_appended_favorite_playlist_has_its_own_sorted_run),
        TEST_CASE(numbered_files_do_not_become_single_item_letter_runs),
        TEST_CASE(section_ranges_cover_each_root_list_contract),
        TEST_CASE(missing_and_spoken_folders_have_no_false_ranges),
        TEST_CASE(lettered_lists_never_go_backwards),
        TEST_CASE(album_tracks_have_no_letters),
        TEST_CASE(invalid_levels_and_missing_favorites_return_no_screen),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
