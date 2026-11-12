#include "check.h"
#include "playback/spoken.h"
#include "playback/spoken_shelf.h"
#include "spoken_library.h"

static SpokenLibrary spoken;

typedef struct FaultSource {
    ByteSource source;
    uint32_t calls;
    uint32_t fail_at;
} FaultSource;

static bool read_with_fault(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    FaultSource *fault = context;
    fault->calls++;
    return fault->calls != fault->fail_at &&
           fault->source.read(fault->source.context, offset, bytes, length);
}

static void folders_named_audiobooks_or_podcasts_hold_spoken_word(void)
{
    CHECK_EQUAL(SPOKEN_AUDIOBOOK, spoken_kind_of("/Audiobooks/Book/01.mp3"));
    CHECK_EQUAL(SPOKEN_AUDIOBOOK, spoken_kind_of("AUDIOBOOK/Book.m4a"));
    CHECK_EQUAL(SPOKEN_PODCAST, spoken_kind_of("/podcasts/Show/Episode.mp3"));
    CHECK_EQUAL(SPOKEN_PODCAST, spoken_kind_of("Podcast/Episode.mp3"));
}

static void m4b_files_are_books_at_every_folder_depth(void)
{
    CHECK_EQUAL(SPOKEN_AUDIOBOOK, spoken_kind_of("Book.m4b"));
    CHECK_EQUAL(SPOKEN_AUDIOBOOK, spoken_kind_of("/Downloads/Writer/Book.M4B"));
    CHECK_EQUAL(SPOKEN_PODCAST, spoken_kind_of("Podcasts/Show/Episode.m4b"));
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of("/Downloads/Book.m4a"));
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of("/Downloads/Book.m4b.mp3"));
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of("/Downloads.m4b/Song.mp3"));
}

static void independent_m4b_files_keep_independent_bookmarks(void)
{
    CHECK_EQUAL(spoken_track_key("Downloads/Book One.m4b"),
                spoken_bookmark_key("Downloads/Book One.m4b"));
    CHECK(spoken_bookmark_key("Downloads/Writer/One.m4b") !=
          spoken_bookmark_key("Downloads/Writer/Two.m4b"));
}

static void other_paths_are_music(void)
{
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of("/Music/Audiobooks/x.mp3"));
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of("/Audiobooks.mp3"));
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of("/Audiobookshelf/x.mp3"));
    CHECK_EQUAL(SPOKEN_NONE, spoken_kind_of(""));
}

static void chapters_of_one_book_share_a_bookmark(void)
{
    uint32_t first = spoken_bookmark_key("/Audiobooks/Book/01.mp3");
    CHECK_EQUAL(first, spoken_bookmark_key("Audiobooks/Book/02.mp3"));
    CHECK(first != spoken_bookmark_key("/Audiobooks/Other/01.mp3"));
}

static void loose_books_and_episodes_have_their_own_bookmark(void)
{
    CHECK(spoken_bookmark_key("/Audiobooks/One.mp3") != spoken_bookmark_key("/Audiobooks/Two.mp3"));
    CHECK(spoken_bookmark_key("/Podcasts/Show/A.mp3") !=
          spoken_bookmark_key("/Podcasts/Show/B.mp3"));
    CHECK_EQUAL(spoken_track_key("/Podcasts/Show/A.mp3"),
                spoken_bookmark_key("Podcasts/Show/A.mp3"));
}

static void the_end_of_a_track_is_its_last_tenth_up_to_30_seconds(void)
{
    CHECK(!spoken_near_end(0, 0));
    CHECK(!spoken_near_end(500000, 600000));
    CHECK(spoken_near_end(570000, 600000));
    CHECK(!spoken_near_end(80000, 100000));
    CHECK(spoken_near_end(90000, 100000));
}

static void near_end_detection_does_not_wrap_for_long_tracks(void)
{
    CHECK(spoken_near_end(UINT32_MAX - 1000, UINT32_MAX));
    CHECK(spoken_near_end(UINT32_MAX, UINT32_MAX));
}

static void the_shelves_are_found_under_the_music_root(void)
{
    Library *library = spoken_library_open(&spoken);
    uint16_t books = 0;
    uint16_t shows = 0;
    CHECK(spoken_shelf_folder(library, SPOKEN_AUDIOBOOK, &books));
    CHECK(spoken_shelf_folder(library, SPOKEN_PODCAST, &shows));
    CHECK(books != shows);
    CHECK(!spoken_shelf_folder(library, SPOKEN_NONE, &books));
}

static void a_book_folder_key_matches_its_chapters(void)
{
    Library *library = &spoken.library;
    uint16_t track = spoken_library_track(library, SPOKEN_CHAPTER_TWO_PATH);
    LibraryTrack record;
    uint32_t key = 0;
    CHECK(library_read_track(library, track, &record));
    CHECK(spoken_folder_key(library, record.folder, &key));
    CHECK_EQUAL(spoken_bookmark_key(SPOKEN_CHAPTER_ONE_PATH), key);
}

static void chapters_are_counted_in_folder_order(void)
{
    Library *library = &spoken.library;
    uint16_t three = spoken_library_track(library, SPOKEN_CHAPTER_THREE_PATH);
    LibraryTrack record;
    uint16_t chapter = 0;
    uint16_t found = 0;
    CHECK(spoken_chapter_of(library, three, &chapter));
    CHECK_EQUAL(2, chapter);
    CHECK(library_read_track(library, three, &record));
    CHECK(spoken_chapter_track(library, record.folder, 2, &found));
    CHECK_EQUAL(three, found);
    CHECK(!spoken_chapter_track(library, record.folder, 3, &found));
}

static void chapters_of_one_file_have_their_own_part_key(void)
{
    CHECK_EQUAL(spoken_track_key("/Audiobooks/Book.m4b"),
                spoken_part_key("Audiobooks/Book.m4b", 0));
    CHECK(spoken_part_key("Audiobooks/Book.m4b", 0) !=
          spoken_part_key("Audiobooks/Book.m4b", 60000));
    CHECK(spoken_part_key("Audiobooks/Book.m4b", 60000) !=
          spoken_part_key("Audiobooks/Book.m4b", 120000));
    CHECK_EQUAL(spoken_part_key("Audiobooks/Book.m4b", 60000),
                spoken_part_key("/Audiobooks/Book.m4b", 60000));
}

static void a_part_of_a_file_is_bookmarked_under_the_file_path(void)
{
    LibraryTrack part = {.start_ms = 60000, .end_ms = 120000};
    LibraryTrack whole = {.start_ms = 0, .end_ms = 0};
    CHECK_EQUAL(spoken_track_key("Audiobooks/Author/Book.m4b"),
                spoken_record_key("Audiobooks/Author/Book.m4b", &part));
    CHECK_EQUAL(spoken_bookmark_key("Audiobooks/Author/Book.m4b"),
                spoken_record_key("Audiobooks/Author/Book.m4b", &whole));
    CHECK(spoken_record_key("Audiobooks/Author/Book.m4b", &part) !=
          spoken_record_key("Audiobooks/Author/Book.m4b", &whole));
}

static bool chapters_folder(const Library *library, uint16_t *folder)
{
    for (uint32_t index = 0; index < library_count(library, LIBRARY_FOLDERS); index++) {
        LibraryFolder record;
        if (library_read_folder(library, index, &record) &&
            (record.flags & LIBRARY_FOLDER_CHAPTERS) != 0) {
            *folder = (uint16_t)index;
            return true;
        }
    }
    return false;
}

static void a_chaptered_book_folder_key_is_its_file_path(void)
{
    Library *library = &spoken.library;
    uint16_t track = spoken_library_chapter(library, 2);
    uint16_t chapter = 0;
    uint16_t sheet = 0;
    uint32_t key = 0;
    CHECK(chapters_folder(library, &sheet));
    CHECK(spoken_chapter_of(library, track, &chapter));
    CHECK_EQUAL(1, chapter);
    CHECK(spoken_folder_holds(library, sheet, track));
    CHECK(spoken_folder_key(library, sheet, &key));
    CHECK_EQUAL(spoken_track_key(SPOKEN_PARTS_PATH), key);
    CHECK_EQUAL(spoken_track_key(SPOKEN_PARTS_PATH),
                spoken_record_key(SPOKEN_PARTS_PATH, &(LibraryTrack){.end_ms = 1}));
}

static void a_folder_holds_exactly_its_own_tracks(void)
{
    Library *library = &spoken.library;
    LibraryTrack record;
    uint16_t chapter_two = spoken_library_chapter(library, 2);
    uint16_t song = spoken_library_track(library, SPOKEN_CHAPTER_TWO_PATH);
    CHECK(library_read_track(library, song, &record));
    CHECK(spoken_folder_holds(library, record.folder, song));
    CHECK(!spoken_folder_holds(library, record.folder, chapter_two));
}

static void external_books_can_be_paged_backwards_without_duplicates(void)
{
    SpokenLibrary external;
    Library *library = spoken_library_open_external(&external);
    SpokenShelf shelf;
    spoken_shelf_init(&shelf, SPOKEN_AUDIOBOOK, NULL, NULL);
    CHECK_EQUAL(3, spoken_shelf_count(library, &shelf));
    BrowseItem first;
    BrowseItem last;
    BrowseItem again;
    CHECK(spoken_shelf_item(library, &shelf, 0, &first));
    CHECK(spoken_shelf_item(library, &shelf, 2, &last));
    CHECK(!spoken_shelf_item(library, &shelf, 3, &again));
    CHECK(spoken_shelf_item(library, &shelf, 0, &again));
    CHECK_EQUAL(first.kind, again.kind);
    CHECK_EQUAL(first.index, again.index);
    CHECK(first.kind != last.kind || first.index != last.index);
    spoken_shelf_init(&shelf, SPOKEN_NONE, NULL, NULL);
    CHECK_EQUAL(0, spoken_shelf_count(library, &shelf));
    CHECK(!spoken_shelf_item(library, &shelf, 0, &again));
    spoken_library_close(&external);
}

static void the_audiobook_shelf_exists_without_a_physical_named_folder(void)
{
    SpokenLibrary external;
    Library *library = spoken_library_open_without_shelf(&external);
    uint16_t folder = 0;
    CHECK(!spoken_shelf_folder(library, SPOKEN_AUDIOBOOK, &folder));
    CHECK(spoken_shelf_present(library, SPOKEN_AUDIOBOOK));
    SpokenShelf shelf;
    spoken_shelf_init(&shelf, SPOKEN_AUDIOBOOK, NULL, NULL);
    CHECK_EQUAL(2, spoken_shelf_count(library, &shelf));
    spoken_shelf_init(&shelf, SPOKEN_PODCAST, NULL, NULL);
    CHECK_EQUAL(1, spoken_shelf_count(library, &shelf));
    spoken_library_close(&external);
}

static void shelf_record_read_failures_never_publish_a_partial_item(void)
{
    SpokenLibrary external;
    Library *library = spoken_library_open_external(&external);
    ByteSource source = library->source;
    FaultSource fault = {.source = source};
    library->source = (ByteSource){read_with_fault, &fault};
    for (uint32_t position = 0; position < 3; position++) {
        SpokenShelf shelf;
        BrowseItem item;
        spoken_shelf_init(&shelf, SPOKEN_AUDIOBOOK, NULL, NULL);
        fault.calls = 0;
        fault.fail_at = 0;
        CHECK(spoken_shelf_item(library, &shelf, position, &item));
        uint32_t reads = fault.calls;
        CHECK(reads > 0);
        for (uint32_t call = 1; call <= reads; call++) {
            spoken_shelf_init(&shelf, SPOKEN_AUDIOBOOK, NULL, NULL);
            fault.calls = 0;
            fault.fail_at = call;
            (void)spoken_shelf_item(library, &shelf, position, &item);
        }
    }
    library->source = source;
    spoken_library_close(&external);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(folders_named_audiobooks_or_podcasts_hold_spoken_word),
        TEST_CASE(other_paths_are_music),
        TEST_CASE(m4b_files_are_books_at_every_folder_depth),
        TEST_CASE(independent_m4b_files_keep_independent_bookmarks),
        TEST_CASE(chapters_of_one_book_share_a_bookmark),
        TEST_CASE(loose_books_and_episodes_have_their_own_bookmark),
        TEST_CASE(the_end_of_a_track_is_its_last_tenth_up_to_30_seconds),
        TEST_CASE(near_end_detection_does_not_wrap_for_long_tracks),
        TEST_CASE(the_shelves_are_found_under_the_music_root),
        TEST_CASE(a_book_folder_key_matches_its_chapters),
        TEST_CASE(chapters_are_counted_in_folder_order),
        TEST_CASE(chapters_of_one_file_have_their_own_part_key),
        TEST_CASE(a_part_of_a_file_is_bookmarked_under_the_file_path),
        TEST_CASE(a_chaptered_book_folder_key_is_its_file_path),
        TEST_CASE(a_folder_holds_exactly_its_own_tracks),
        TEST_CASE(external_books_can_be_paged_backwards_without_duplicates),
        TEST_CASE(the_audiobook_shelf_exists_without_a_physical_named_folder),
        TEST_CASE(shelf_record_read_failures_never_publish_a_partial_item),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    spoken_library_close(&spoken);
    return result;
}
