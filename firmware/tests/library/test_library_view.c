#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "library/library_view.h"

typedef struct Work {
    uint8_t bytes[LIBRARY_VIEW_WORK_BYTES];
    bool fail_read;
    bool fail_write;
    uint32_t reads;
    uint32_t writes;
} Work;

static FixtureLibrary fixture;
static LibraryView view;
static Work work;

static bool read_work(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    Work *file = context;
    file->reads++;
    if (file->fail_read || offset > sizeof file->bytes || length > sizeof file->bytes - offset) {
        return false;
    }
    memcpy(bytes, file->bytes + offset, length);
    return true;
}

static bool write_work(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Work *file = context;
    file->writes++;
    if (file->fail_write || offset > sizeof file->bytes || length > sizeof file->bytes - offset) {
        return false;
    }
    memcpy(file->bytes + offset, bytes, length);
    return true;
}

static BuilderScratch scratch(void)
{
    return (BuilderScratch){read_work, write_work, &work, sizeof work.bytes};
}

static LibraryViewState finish(void)
{
    for (uint32_t step = 0; step < 2000000 && (view.state == LIBRARY_VIEW_SCANNING ||
                                               view.state == LIBRARY_VIEW_SORTING);
         step++) {
        library_view_step(&view);
    }
    return view.state;
}

static LibraryViewConfig begin_fixture(void)
{
    memset(&work, 0, sizeof work);
    fixture_library_open(&fixture);
    return library_view_defaults();
}

static bool results_are(const uint16_t *wanted, uint16_t count)
{
    uint16_t found[32];
    return view.count == count && count <= 32 && library_view_read(&view, 0, count, found) &&
           memcmp(found, wanted, count * sizeof *wanted) == 0;
}

static void folded_substring_search_preserves_original_track_identity(void)
{
    LibraryViewConfig config = begin_fixture();
    memcpy(config.text, "JOGA", 5);
    config.fields = LIBRARY_VIEW_FIELD_TITLE;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {6};
    CHECK(results_are(wanted, 1));
}

static void artist_search_is_accent_insensitive_and_articles_remain_searchable(void)
{
    LibraryViewConfig config = begin_fixture();
    memcpy(config.text, "BJORK", 6);
    config.fields = LIBRARY_VIEW_FIELD_ARTIST;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t artist[] = {3, 5, 6};
    CHECK(results_are(artist, 3));
    memcpy(config.text, "the", 4);
    config.fields = LIBRARY_VIEW_FIELD_ALBUM;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t albums[] = {3, 2, 7};
    CHECK(results_are(albums, 3));
}

static void filters_combine_inclusively_and_an_empty_result_is_success(void)
{
    LibraryViewConfig config = begin_fixture();
    config.artist = 3;
    config.genre = 0;
    config.album = 3;
    config.year_min = config.year_max = 1997;
    config.duration_min_ms = config.duration_max_ms = 60000;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {5, 6};
    CHECK(results_are(wanted, 2));
    config.duration_min_ms = 60001;
    config.duration_max_ms = 0;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    CHECK_EQUAL(0, view.count);
}

static void year_sort_is_descending_and_equal_keys_keep_source_order(void)
{
    LibraryViewConfig config = begin_fixture();
    config.sort = LIBRARY_VIEW_SORT_YEAR;
    config.descending = true;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {7, 3, 2, 5, 6, 0, 1, 4};
    CHECK(results_are(wanted, 8));
}

static void original_direction_and_duration_ties_do_not_renumber_tracks(void)
{
    LibraryViewConfig config = begin_fixture();
    config.descending = true;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t reverse[] = {7, 1, 6, 4, 5, 2, 0, 3};
    CHECK(results_are(reverse, 8));
    config.sort = LIBRARY_VIEW_SORT_DURATION;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t stable[] = {3, 0, 2, 5, 4, 6, 1, 7};
    CHECK(results_are(stable, 8));
}

static void cancellation_and_content_changes_make_results_unavailable(void)
{
    LibraryViewConfig config = begin_fixture();
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_SCANNING, view.state);
    library_view_step(&view);
    library_view_cancel(&view);
    CHECK_EQUAL(LIBRARY_VIEW_CANCELLED, library_view_step(&view));
    uint16_t track;
    CHECK(!library_view_read(&view, 0, 1, &track));
    library_view_begin(&view, &fixture.library, &config, scratch());
    fixture.library.content_fingerprint++;
    CHECK_EQUAL(LIBRARY_VIEW_CHANGED, library_view_step(&view));
}

static void storage_and_library_read_errors_are_not_empty_search_results(void)
{
    LibraryViewConfig config = begin_fixture();
    library_view_begin(&view, &fixture.library, &config, scratch());
    fixture.memory.failing = true;
    CHECK_EQUAL(LIBRARY_VIEW_READ_FAILED, finish());
    fixture.memory.failing = false;
    work.fail_write = true;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_WRITE_FAILED, finish());
    work.fail_write = false;
    config.sort = LIBRARY_VIEW_SORT_YEAR;
    library_view_begin(&view, &fixture.library, &config, scratch());
    while (view.state == LIBRARY_VIEW_SCANNING) {
        library_view_step(&view);
    }
    work.fail_read = true;
    CHECK_EQUAL(LIBRARY_VIEW_READ_FAILED, finish());
}

static void title_artist_and_album_sorts_are_stable_in_both_directions(void)
{
    LibraryViewConfig config = begin_fixture();
    config.sort = LIBRARY_VIEW_SORT_TITLE;
    config.descending = true;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t titles[] = {7, 1, 6, 4, 5, 2, 0, 3};
    CHECK(results_are(titles, 8));
    config.sort = LIBRARY_VIEW_SORT_ARTIST;
    config.descending = false;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t artists[] = {0, 4, 1, 7, 2, 3, 5, 6};
    CHECK(results_are(artists, 8));
    config.sort = LIBRARY_VIEW_SORT_ALBUM;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t albums[] = {0, 1, 3, 2, 4, 5, 6, 7};
    CHECK(results_are(albums, 8));
}

static void metadata_search_can_select_album_artist_genre_and_path_fields(void)
{
    const uint32_t fields[] = {LIBRARY_VIEW_FIELD_ALBUM_ARTIST, LIBRARY_VIEW_FIELD_ALBUM,
                               LIBRARY_VIEW_FIELD_PATH};
    for (size_t index = 0; index < sizeof fields / sizeof *fields; index++) {
        LibraryViewConfig config = begin_fixture();
        const char *text = index == 0 ? "bjork" : "homogenic";
        memcpy(config.text, text, strlen(text) + 1);
        config.fields = fields[index];
        library_view_begin(&view, &fixture.library, &config, scratch());
        CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
        const uint16_t wanted[] = {5, 6};
        CHECK(results_are(wanted, 2));
    }
    LibraryViewConfig config = begin_fixture();
    memcpy(config.text, "POP", 4);
    config.fields = LIBRARY_VIEW_FIELD_GENRE;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t pop[] = {3, 2};
    CHECK(results_are(pop, 2));
}

static LibraryViewAcceptance accept_one(void *context, uint16_t track, const LibraryTrack *record)
{
    bool failed = *(bool *)context;
    CHECK(record->artist == 3);
    return failed       ? LIBRARY_VIEW_ACCEPT_FAILED
           : track == 5 ? LIBRARY_VIEW_ACCEPT
                        : LIBRARY_VIEW_REJECT;
}

static void personal_tag_predicates_combine_with_metadata_and_propagate_errors(void)
{
    LibraryViewConfig config = begin_fixture();
    bool failed = false;
    config.artist = 3;
    config.accept = accept_one;
    config.context = &failed;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {5};
    CHECK(results_are(wanted, 1));
    failed = true;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READ_FAILED, finish());
}

static void album_and_playlist_scopes_keep_only_their_original_ids_and_duplicates(void)
{
    LibraryViewConfig config = begin_fixture();
    config.source = browse_root_query(BROWSE_ALBUM_TRACKS);
    config.source.subject = 3;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t album[] = {5, 6};
    CHECK(results_are(album, 2));
    LibraryExtraPlaylist extra = {.name = "Saved", .playlist = 0, .count = 3, .tracks = {6, 3, 6}};
    fixture.library.extra = &extra;
    config.source = browse_root_query(BROWSE_PLAYLIST_TRACKS);
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    CHECK(results_are(extra.tracks, 3));
    config.sort = LIBRARY_VIEW_SORT_TITLE;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t sorted[] = {3, 6, 6};
    CHECK(results_are(sorted, 3));
}

static void folder_tree_traversal_keeps_cue_parts_without_parent_duplicates(void)
{
    begin_fixture();
    fixture_cue_library_open(&fixture);
    LibraryViewConfig config = library_view_defaults();
    config.source = browse_root_query(BROWSE_FOLDER_TREE);
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    uint32_t count = browse_count(&fixture.library, config.source);
    CHECK_EQUAL(count, view.count);
    for (uint32_t row = 0; row < count; row++) {
        BrowseItem item;
        uint16_t actual = LIBRARY_NONE;
        CHECK(browse_item(&fixture.library, config.source, row, &item));
        CHECK(library_view_read(&view, (uint16_t)row, 1, &actual));
        CHECK_EQUAL(item.index, actual);
    }
}

static void cancelling_a_sort_cannot_publish_partial_rows(void)
{
    LibraryViewConfig config = begin_fixture();
    config.sort = LIBRARY_VIEW_SORT_YEAR;
    library_view_begin(&view, &fixture.library, &config, scratch());
    while (view.state == LIBRARY_VIEW_SCANNING) {
        library_view_step(&view);
    }
    CHECK_EQUAL(LIBRARY_VIEW_SORTING, view.state);
    library_view_step(&view);
    library_view_cancel(&view);
    CHECK_EQUAL(LIBRARY_VIEW_CANCELLED, library_view_step(&view));
    uint16_t actual;
    CHECK(!library_view_read(&view, 0, 1, &actual));
}

enum {
    LARGE_TITLES_OFFSET = 3000000
};

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    put16(bytes, (uint16_t)value);
    put16(bytes + 2, (uint16_t)(value >> 16));
}

static bool read_large(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t *out = bytes;
    if (offset < UINT16_MAX * 32u && offset % 32u == 0 && length == 32) {
        memset(out, 0, length);
        put32(out + 16, offset / 32u + 1u);
        return true;
    }
    if (offset >= LARGE_TITLES_OFFSET && offset < LARGE_TITLES_OFFSET + UINT16_MAX * 2u &&
        (offset - LARGE_TITLES_OFFSET) % 2u == 0 && length == 2) {
        put16(out, (uint16_t)((offset - LARGE_TITLES_OFFSET) / 2u));
        return true;
    }
    return false;
}

static void a_full_65535_track_catalogue_sorts_without_a_resident_result_array(void)
{
    memset(&work, 0, sizeof work);
    Library library = {
        .source = {read_large, NULL}, .content_known = true, .content_fingerprint = 1234};
    library.sections[LIBRARY_TRACKS] = (LibrarySectionInfo){0, UINT16_MAX, 32, UINT16_MAX};
    library.sections[LIBRARY_TITLE_ORDER] =
        (LibrarySectionInfo){LARGE_TITLES_OFFSET, UINT16_MAX, 2, UINT16_MAX};
    LibraryViewConfig config = library_view_defaults();
    config.sort = LIBRARY_VIEW_SORT_DURATION;
    config.descending = true;
    library_view_begin(&view, &library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    CHECK_EQUAL(UINT16_MAX, view.count);
    uint16_t found[LIBRARY_VIEW_PAGE_TRACKS];
    for (uint32_t first = 0; first < UINT16_MAX; first += LIBRARY_VIEW_PAGE_TRACKS) {
        uint32_t count = UINT16_MAX - first;
        count = count < LIBRARY_VIEW_PAGE_TRACKS ? count : LIBRARY_VIEW_PAGE_TRACKS;
        CHECK(library_view_source_read(&view, (uint16_t)first, (uint16_t)count, found));
        for (uint32_t index = 0; index < count; index++) {
            CHECK_EQUAL(UINT16_MAX - 1u - first - index, found[index]);
        }
    }
    CHECK(!library_view_read(&view, UINT16_MAX, 1, found));
    CHECK(sizeof view <= 4096);
}

static void fixture_track_path(const char *path)
{
    LibrarySectionInfo *strings = &fixture.library.sections[LIBRARY_STRINGS];
    uint32_t offset = strings->count;
    uint32_t bytes = (uint32_t)strlen(path) + 1;
    uint32_t end = strings->offset + offset + bytes;
    CHECK(end <= sizeof fixture.bytes);
    memcpy(fixture.bytes + strings->offset + offset, path, bytes);
    strings->count += bytes;
    put32(fixture.bytes + 8 + LIBRARY_STRINGS * 12 + 4, strings->count);
    put32(fixture.bytes + fixture.library.sections[LIBRARY_TRACKS].offset + 4, offset);
    if (end > fixture.size) {
        fixture.size = end;
    }
    fixture.memory.size = fixture.size;
    CHECK(fixture_library_identify(&fixture.library));
}

static void path_suffixes_beyond_the_metadata_display_limit_remain_searchable(void)
{
    LibraryViewConfig config = begin_fixture();
    char path[259];
    memset(path, 'a', sizeof path - 1);
    memcpy(path + 255, "zzz", 4);
    fixture_track_path(path);
    memcpy(config.text, "zzz", 4);
    config.fields = LIBRARY_VIEW_FIELD_PATH;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {0};
    CHECK(results_are(wanted, 1));
}

static void folded_path_expansion_does_not_hide_a_later_suffix(void)
{
    LibraryViewConfig config = begin_fixture();
    char path[512];
    for (size_t index = 0; index < 140; index++) {
        memcpy(path + index * 3, "\xEF\xB7\xBA", 3);
    }
    memcpy(path + 420, "/Needle.mp3", 12);
    fixture_track_path(path);
    memcpy(config.text, "needle", 7);
    config.fields = LIBRARY_VIEW_FIELD_PATH;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {0};
    CHECK(results_are(wanted, 1));
}

static void path_search_matches_across_normalization_chunks_and_spaces(void)
{
    LibraryViewConfig config = begin_fixture();
    char path[96] = {0};
    memset(path, 'a', 29);
    memcpy(path + 29, "The Night Drive/song.flac", 26);
    fixture_track_path(path);
    memcpy(config.text, "THE NIGHT DRIVE", 16);
    config.fields = LIBRARY_VIEW_FIELD_PATH;
    library_view_begin(&view, &fixture.library, &config, scratch());
    CHECK_EQUAL(LIBRARY_VIEW_READY, finish());
    const uint16_t wanted[] = {0};
    CHECK(results_are(wanted, 1));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(path_suffixes_beyond_the_metadata_display_limit_remain_searchable),
        TEST_CASE(folded_path_expansion_does_not_hide_a_later_suffix),
        TEST_CASE(path_search_matches_across_normalization_chunks_and_spaces),
        TEST_CASE(title_artist_and_album_sorts_are_stable_in_both_directions),
        TEST_CASE(metadata_search_can_select_album_artist_genre_and_path_fields),
        TEST_CASE(personal_tag_predicates_combine_with_metadata_and_propagate_errors),
        TEST_CASE(album_and_playlist_scopes_keep_only_their_original_ids_and_duplicates),
        TEST_CASE(folder_tree_traversal_keeps_cue_parts_without_parent_duplicates),
        TEST_CASE(cancelling_a_sort_cannot_publish_partial_rows),
        TEST_CASE(a_full_65535_track_catalogue_sorts_without_a_resident_result_array),
        TEST_CASE(folded_substring_search_preserves_original_track_identity),
        TEST_CASE(artist_search_is_accent_insensitive_and_articles_remain_searchable),
        TEST_CASE(filters_combine_inclusively_and_an_empty_result_is_success),
        TEST_CASE(year_sort_is_descending_and_equal_keys_keep_source_order),
        TEST_CASE(original_direction_and_duration_ties_do_not_renumber_tracks),
        TEST_CASE(cancellation_and_content_changes_make_results_unavailable),
        TEST_CASE(storage_and_library_read_errors_are_not_empty_search_results),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
