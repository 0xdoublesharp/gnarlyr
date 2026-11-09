#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "hal/storage.h"
#include "library/library.h"
#include "library/library_browse.h"
#include "library/library_scan.h"
#include "library_compare.h"
#include "sim_storage.h"

enum {
    MEMORY_SIZE = 384 * 1024,
    OUTPUT_CAPACITY = 1024 * 1024,
    SCRATCH_SIZE = 16 * 1024 * 1024
};

typedef struct HostFiles {
    HalDir dir;
    HalFile file;
    HalDirEntry entry;
} HostFiles;

typedef struct Output {
    uint8_t bytes[OUTPUT_CAPACITY];
    uint32_t length;
} Output;

static bool open_dir(void *context, const char *path)
{
    HostFiles *files = context;
    return hal_dir_open(&files->dir, path);
}

static bool next_entry(void *context, ScanEntry *entry)
{
    HostFiles *files = context;
    if (!hal_dir_next(&files->dir, &files->entry)) {
        return false;
    }
    entry->directory = files->entry.directory;
    memcpy(entry->name, files->entry.name, sizeof entry->name);
    return true;
}

static void close_dir(void *context)
{
    HostFiles *files = context;
    hal_dir_close(&files->dir);
}

static bool read_host(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    HostFiles *files = context;
    return hal_file_read(files->file, offset, buffer, length);
}

static bool open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    HostFiles *files = context;
    if (!hal_file_open(&files->file, path)) {
        return false;
    }
    *source = (ByteSource){.read = read_host, .context = files};
    *size = files->file.size;
    return true;
}

static void close_file(void *context)
{
    HostFiles *files = context;
    hal_file_close(&files->file);
}

static bool write_output(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Output *output = context;
    if (offset + length > OUTPUT_CAPACITY) {
        return false;
    }
    memcpy(output->bytes + offset, bytes, length);
    output->length = offset + length > output->length ? offset + length : output->length;
    return true;
}

static uint8_t *scratch_bytes(void)
{
    static uint8_t *bytes;
    if (bytes == NULL) {
        bytes = calloc(SCRATCH_SIZE, 1);
    }
    return bytes;
}

static bool scratch_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (offset > SCRATCH_SIZE || length > SCRATCH_SIZE - offset) {
        return false;
    }
    memcpy(buffer, scratch_bytes() + offset, length);
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    if (offset > SCRATCH_SIZE || length > SCRATCH_SIZE - offset) {
        return false;
    }
    memcpy(scratch_bytes() + offset, bytes, length);
    return true;
}

static const BuilderScratch SCRATCH = {scratch_read, scratch_write, NULL, SCRATCH_SIZE};

typedef struct Progress {
    uint32_t calls;
    uint32_t last_tracks;
} Progress;

static void report(void *context, uint32_t files, uint32_t tracks)
{
    Progress *progress = context;
    (void)files;
    progress->calls++;
    progress->last_tracks = tracks;
}

static uint8_t *read_fixture(const char *name, uint32_t *size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    fseek(file, 0, SEEK_END);
    *size = (uint32_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = malloc(*size);
    CHECK(fread(bytes, 1, *size, file) == *size);
    fclose(file);
    return bytes;
}

static ScanResult scan_folder(const char *folder, Output *output, Progress *progress,
                              ScanStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    static HostFiles files;
    sim_storage_configure(folder, NULL);
    output->length = 0;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
        .memory = memory,
        .size = sizeof memory,
        .scratch = SCRATCH,
        .sink = {write_output, output},
        .progress = {report, progress},
    };
    return library_scan(&job, stats);
}

static void scanning_the_tag_fixtures_matches_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/tags", &output, &progress, &stats));
    CHECK_EQUAL(13u, stats.tracks);
    CHECK_EQUAL(13u, progress.last_tracks);
    CHECK(progress.calls >= 13);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("tags_library.db", &expected_size);
    CHECK_EQUAL(stats.length, output.length);
    CHECK(library_compare(expected, expected_size, output.bytes, output.length));
    free(expected);
}

static void scanning_folders_and_playlists_matches_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/scan_music", &output, &progress, &stats));
    CHECK_EQUAL(4u, stats.tracks);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("scan_music.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

static void a_separate_listing_leaves_the_memory_to_the_builder(void)
{
    static uint8_t memory[128 * 1024];
    static uint8_t listing[16 * 1024];
    static HostFiles files;
    static Output output;
    sim_storage_configure(ECHO_FIXTURE_DIR "/scan_music", NULL);
    output.length = 0;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
        .memory = memory,
        .size = sizeof memory,
        .listing = listing,
        .listing_size = sizeof listing,
        .scratch = SCRATCH,
        .sink = {write_output, &output},
    };
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, library_scan(&job, &stats));
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("scan_music.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

static void a_missing_music_root_says_so(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_NO_MUSIC_ROOT,
                scan_folder(ECHO_FIXTURE_DIR "/no such folder", &output, &progress, &stats));
}

enum {
    TREE_CAPACITY = 4096,
    TREE_PATH = 96,
    FRAME_BYTES = 417,
    FRAMES = 3
};

typedef struct TreeEntry {
    char name[TREE_PATH];
    bool directory;
} TreeEntry;

typedef struct Tree {
    char paths[TREE_CAPACITY][TREE_PATH];
    uint32_t count;
    TreeEntry listing[TREE_CAPACITY];
    uint32_t listed;
    uint32_t cursor;
    uint32_t opens;
    uint8_t blob[FRAMES * FRAME_BYTES];
} Tree;

static Tree tree;

static void tree_add(const char *path)
{
    snprintf(tree.paths[tree.count++], TREE_PATH, "%s", path);
}

static bool tree_open_dir(void *context, const char *path)
{
    (void)context;
    size_t prefix = strlen(path);
    tree.listed = 0;
    tree.cursor = 0;
    tree.opens++;
    for (uint32_t index = 0; index < tree.count; index++) {
        const char *file = tree.paths[index];
        if (strncmp(file, path, prefix) != 0) {
            continue;
        }
        const char *rest = file + prefix;
        const char *slash = strchr(rest, '/');
        size_t length = slash == NULL ? strlen(rest) : (size_t)(slash - rest);
        bool seen = false;
        for (uint32_t other = 0; other < tree.listed && !seen; other++) {
            seen = strlen(tree.listing[other].name) == length &&
                   strncmp(tree.listing[other].name, rest, length) == 0;
        }
        if (!seen) {
            TreeEntry *entry = &tree.listing[tree.listed++];
            snprintf(entry->name, TREE_PATH, "%.*s", (int)length, rest);
            entry->directory = slash != NULL;
        }
    }
    return tree.listed > 0;
}

static bool tree_next_entry(void *context, ScanEntry *entry)
{
    (void)context;
    if (tree.cursor >= tree.listed) {
        return false;
    }
    const TreeEntry *next = &tree.listing[tree.listed - 1 - tree.cursor++];
    entry->directory = next->directory;
    snprintf(entry->name, sizeof entry->name, "%s", next->name);
    return true;
}

static void tree_close_dir(void *context)
{
    (void)context;
}

static bool tree_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (offset > sizeof tree.blob || length > sizeof tree.blob - offset) {
        return false;
    }
    memcpy(buffer, tree.blob + offset, length);
    return true;
}

static bool tree_open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    (void)context;
    (void)path;
    *source = (ByteSource){.read = tree_read, .context = NULL};
    *size = sizeof tree.blob;
    return true;
}

static void tree_close_file(void *context)
{
    (void)context;
}

static void start_tree(void)
{
    tree.count = 0;
    tree.opens = 0;
    memset(tree.blob, 0, sizeof tree.blob);
    for (uint32_t frame = 0; frame < FRAMES; frame++) {
        uint8_t *header = tree.blob + frame * FRAME_BYTES;
        header[0] = 0xFF;
        header[1] = 0xFB;
        header[2] = 0x90;
        header[3] = 0xC0;
    }
}

static ScanResult scan_tree(uint32_t listing_size, Output *output, ScanStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    static uint8_t listing[96 * 1024];
    output->length = 0;
    ScanJob job = {
        .files = {tree_open_dir, tree_next_entry, tree_close_dir, tree_open_file, tree_close_file,
                  NULL},
        .memory = memory,
        .size = sizeof memory,
        .listing = listing,
        .listing_size = listing_size,
        .scratch = SCRATCH,
        .sink = {write_output, output},
    };
    return library_scan(&job, stats);
}

static bool output_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const Output *output = context;
    if (offset > output->length || length > output->length - offset) {
        return false;
    }
    memcpy(buffer, output->bytes + offset, length);
    return true;
}

static bool library_holds_every_path(Output *output)
{
    Library library;
    if (library_open(&library, (ByteSource){output_read, output}, output->length) != LIBRARY_OK ||
        library_count(&library, LIBRARY_TRACKS) != tree.count) {
        return false;
    }
    static bool found[TREE_CAPACITY];
    memset(found, 0, sizeof found);
    for (uint32_t index = 0; index < tree.count; index++) {
        LibraryTrack track;
        char path[TREE_PATH + 1];
        path[0] = '/';
        if (!library_read_track(&library, index, &track) ||
            !library_read_string(&library, track.path, path + 1, sizeof path - 1)) {
            return false;
        }
        for (uint32_t file = 0; file < tree.count; file++) {
            if (!found[file] && strcmp(tree.paths[file], path) == 0) {
                found[file] = true;
                break;
            }
        }
    }
    for (uint32_t file = 0; file < tree.count; file++) {
        if (!found[file]) {
            return false;
        }
    }
    return true;
}

static void a_folder_bigger_than_the_listing_is_read_in_batches(void)
{
    static Output output;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t index = 0; index < 3000; index++) {
        snprintf(path, sizeof path, "/Big/%04u - %s %04u.mp3", index, index % 3 ? "song" : "Song",
                 index * 7919 % 3000);
        tree_add(path);
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &output, &stats));
    CHECK_EQUAL(3000u, stats.tracks);
    CHECK(library_holds_every_path(&output));
    CHECK(tree.opens > 2);
}

static void many_sibling_folders_fit_a_small_listing(void)
{
    static Output output;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t index = 0; index < 700; index++) {
        snprintf(path, sizeof path, "/Music/Artist %03u/Album/01 - One.mp3", index);
        tree_add(path);
        snprintf(path, sizeof path, "/Music/Artist %03u/Album/02 - Two.mp3", index);
        tree_add(path);
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &output, &stats));
    CHECK_EQUAL(1400u, stats.tracks);
    CHECK(library_holds_every_path(&output));
}

static void a_big_folder_nested_in_big_folders_is_read_in_batches(void)
{
    static Output output;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t outer = 0; outer < 700; outer++) {
        for (uint32_t inner = 0; inner < 5; inner++) {
            snprintf(path, sizeof path, "/Outer folder %03u/Inner %u.mp3", outer, inner);
            tree_add(path);
        }
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &output, &stats));
    CHECK_EQUAL(3500u, stats.tracks);
    CHECK(library_holds_every_path(&output));
}

static void batches_build_the_same_library_as_one_whole_read(void)
{
    static Output whole;
    static Output batched;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t outer = 0; outer < 700; outer++) {
        for (uint32_t inner = 0; inner < 5; inner++) {
            snprintf(path, sizeof path, "/Outer folder %03u/Inner %u.mp3", (outer * 37) % 700,
                     inner);
            tree_add(path);
        }
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(96 * 1024, &whole, &stats));
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &batched, &stats));
    CHECK(library_compare(whole.bytes, whole.length, batched.bytes, batched.length));
}

typedef struct CueCheck {
    uint32_t count;
    uint32_t starts[4];
    uint32_t ends[4];
    uint32_t durations[4];
    char titles[4][80];
} CueCheck;

static void collect_cue_tracks(Output *output, const char *wanted_path, CueCheck *check)
{
    Library library;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&library, (ByteSource){output_read, output}, output->length));
    *check = (CueCheck){.count = 0};
    for (uint32_t index = 0; index < library_count(&library, LIBRARY_TRACKS); index++) {
        LibraryTrack track;
        char path[TREE_PATH];
        CHECK(library_read_track(&library, index, &track));
        CHECK(library_read_string(&library, track.path, path, sizeof path));
        if (strcmp(path, wanted_path) != 0 || check->count == 4) {
            continue;
        }
        check->starts[check->count] = track.start_ms;
        check->ends[check->count] = track.end_ms;
        check->durations[check->count] = track.duration_ms;
        CHECK(library_read_string(&library, track.title, check->titles[check->count], 80));
        check->count++;
    }
}

static void scanning_cue_sheets_matches_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/cue_music", &output, &progress, &stats));
    CHECK_EQUAL(9u, stats.tracks);
    CHECK_EQUAL(9u, progress.last_tracks);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("cue_music.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

typedef struct PlaylistCheck {
    uint32_t count;
    char titles[8][32];
} PlaylistCheck;

static void collect_playlist(Output *output, const char *name, PlaylistCheck *check)
{
    Library library;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&library, (ByteSource){output_read, output}, output->length));
    *check = (PlaylistCheck){.count = 0};
    for (uint32_t index = 0; index < library_count(&library, LIBRARY_PLAYLISTS); index++) {
        LibraryGroup group;
        char found[64];
        CHECK(library_read_group(&library, LIBRARY_PLAYLISTS, index, &group));
        CHECK(library_read_string(&library, group.name, found, sizeof found));
        if (strcmp(found, name) != 0) {
            continue;
        }
        CHECK(group.link_count <= 8);
        check->count = group.link_count;
        for (uint32_t link = 0; link < group.link_count && link < 8; link++) {
            uint16_t position = 0;
            LibraryTrack track;
            CHECK(library_read_index(&library, LIBRARY_LINKS, group.first_link + link, &position));
            CHECK(library_read_track(&library, position, &track));
            CHECK(library_read_string(&library, track.title, check->titles[link], 32));
        }
        return;
    }
    check->count = UINT32_MAX;
}

static void a_favorites_file_in_the_music_root_resolves_each_cue_part(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    PlaylistCheck check;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/cue_music", &output, &progress, &stats));
    collect_playlist(&output, "Favorite Tracks", &check);
    CHECK_EQUAL(6u, check.count);
    static const char *const WANTED[] = {"Middle Part", "Opening", "Closing",
                                         "Closing",     "Closing", "a"};
    for (uint32_t index = 0; index < 6; index++) {
        CHECK(strncmp(check.titles[index], WANTED[index], strlen(WANTED[index])) == 0);
    }
}

static void a_padded_favorites_file_without_directives_resolves_to_the_last_part(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    PlaylistCheck check;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/cue_music", &output, &progress, &stats));
    collect_playlist(&output, "Legacy Favorites", &check);
    CHECK_EQUAL(1u, check.count);
    CHECK(strcmp(check.titles[0], "Closing") == 0);
}

static void scanning_m4a_files_matches_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/m4a_music", &output, &progress, &stats));
    CHECK_EQUAL(6u, stats.tracks);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("m4a_music.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

static void a_cue_sheet_makes_a_track_per_cue_track_and_hides_its_audio(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CueCheck check;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/cue_music", &output, &progress, &stats));
    collect_cue_tracks(&output, "Cue Artist/Cue Album/Cue Album.flac", &check);
    CHECK_EQUAL(3u, check.count);
    CHECK_EQUAL(0u, check.starts[0]);
    CHECK_EQUAL(1000u, check.starts[1]);
    CHECK_EQUAL(2493u, check.starts[2]);
    CHECK_EQUAL(1000u, check.ends[0]);
    CHECK_EQUAL(2493u, check.ends[1]);
    CHECK_EQUAL(0u, check.ends[2]);
    CHECK_EQUAL(1507u, check.durations[2]);
    CHECK(strcmp(check.titles[0], "Opening") == 0);
    CHECK(strcmp(check.titles[2], "Closing") == 0);
}

static void a_cue_sheet_that_cannot_be_used_leaves_the_audio_alone(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CueCheck check;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/cue_music", &output, &progress, &stats));
    collect_cue_tracks(&output, "Plain/Unsupported/a.wav", &check);
    CHECK_EQUAL(1u, check.count);
    CHECK_EQUAL(0u, check.ends[0]);
    CHECK_EQUAL(1000u, check.durations[0]);
}

static const char *const AUDIOBOOKS = "Audiobooks/";

static void collect_book(Output *output, const char *file, CueCheck *check)
{
    char path[TREE_PATH];
    snprintf(path, sizeof path, "%s%s", AUDIOBOOKS, file);
    collect_cue_tracks(output, path, check);
}

static void scan_books(Output *output, ScanStats *stats)
{
    Progress progress = {.calls = 0};
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/m4b_music", output, &progress, stats));
}

static void an_m4b_with_a_chpl_atom_has_a_track_per_chapter(void)
{
    static Output output;
    ScanStats stats;
    CueCheck check;
    scan_books(&output, &stats);
    CHECK_EQUAL(16u, stats.tracks);
    collect_book(&output, "Chpl Book.m4b", &check);
    CHECK_EQUAL(3u, check.count);
    CHECK_EQUAL(0u, check.starts[0]);
    CHECK_EQUAL(100000u, check.starts[1]);
    CHECK_EQUAL(220000u, check.starts[2]);
    CHECK_EQUAL(100000u, check.ends[0]);
    CHECK_EQUAL(220000u, check.ends[1]);
    CHECK_EQUAL(0u, check.ends[2]);
    CHECK_EQUAL(100000u, check.durations[0]);
    CHECK_EQUAL(120000u, check.durations[1]);
    CHECK_EQUAL(80000u, check.durations[2]);
    CHECK(strcmp(check.titles[0], "Opening") == 0);
    CHECK(strcmp(check.titles[1], "The Middle") == 0);
    CHECK(strcmp(check.titles[2], "Finale") == 0);
}

static void a_chpl_atom_without_the_reserved_word_is_read_too(void)
{
    static Output output;
    ScanStats stats;
    CueCheck check;
    scan_books(&output, &stats);
    collect_book(&output, "Nero Book.m4b", &check);
    CHECK_EQUAL(2u, check.count);
    CHECK_EQUAL(50000u, check.starts[1]);
    CHECK_EQUAL(70000u, check.durations[1]);
    CHECK(strcmp(check.titles[0], "Part One") == 0);
    CHECK(strcmp(check.titles[1], "Part Two") == 0);
}

static void an_m4b_with_a_chapter_text_track_has_a_track_per_sample(void)
{
    static Output output;
    ScanStats stats;
    CueCheck check;
    scan_books(&output, &stats);
    collect_book(&output, "Track Book.m4b", &check);
    CHECK_EQUAL(4u, check.count);
    CHECK_EQUAL(0u, check.starts[0]);
    CHECK_EQUAL(60000u, check.starts[1]);
    CHECK_EQUAL(120000u, check.starts[2]);
    CHECK_EQUAL(210000u, check.starts[3]);
    CHECK_EQUAL(0u, check.ends[3]);
    CHECK_EQUAL(90000u, check.durations[3]);
    CHECK(strcmp(check.titles[0], "One") == 0);
    CHECK(strcmp(check.titles[2], "Three") == 0);
    CHECK(strcmp(check.titles[3], "Four") == 0);
}

static void an_m4b_without_usable_chapters_is_one_track(void)
{
    static const char *const FILES[] = {"Plain Book.m4b", "Single Chapter.m4b", "Broken Chpl.m4b",
                                        "Broken Track.m4b"};
    static Output output;
    ScanStats stats;
    scan_books(&output, &stats);
    for (size_t index = 0; index < sizeof FILES / sizeof FILES[0]; index++) {
        CueCheck check;
        collect_book(&output, FILES[index], &check);
        CHECK_EQUAL(1u, check.count);
        CHECK_EQUAL(0u, check.starts[0]);
        CHECK_EQUAL(0u, check.ends[0]);
    }
}

static void odd_chapter_lists_are_cleaned_up(void)
{
    static Output output;
    ScanStats stats;
    CueCheck check;
    scan_books(&output, &stats);
    collect_book(&output, "Messy Chpl.m4b", &check);
    CHECK_EQUAL(3u, check.count);
    CHECK_EQUAL(100000u, check.starts[1]);
    CHECK_EQUAL(150000u, check.starts[2]);
    CHECK_EQUAL(150000u, check.durations[2]);
    CHECK(strcmp(check.titles[0], "A") == 0);
    CHECK_EQUAL(62u, strlen(check.titles[1]));
    CHECK(strcmp(check.titles[2], "Chapter 3") == 0);
}

static void real_ffmpeg_chapters_are_read_from_either_style(void)
{
    static const char *const FILES[] = {"Real Book.m4b", "Real Track.m4b"};
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(ECHO_FIXTURE_DIR "/m4b_real", &output, &progress, &stats));
    CHECK_EQUAL(6u, stats.tracks);
    for (size_t index = 0; index < sizeof FILES / sizeof FILES[0]; index++) {
        CueCheck check;
        collect_book(&output, FILES[index], &check);
        CHECK_EQUAL(3u, check.count);
        CHECK_EQUAL(400u, check.starts[1]);
        CHECK_EQUAL(700u, check.starts[2]);
        CHECK(strcmp(check.titles[0], "Intro") == 0);
        CHECK(strcmp(check.titles[1], "Middle") == 0);
        CHECK(strcmp(check.titles[2], "End") == 0);
    }
}

static bool find_folder(const Library *library, const char *name, LibraryFolder *folder)
{
    for (uint32_t index = 0; index < library_count(library, LIBRARY_FOLDERS); index++) {
        char text[TREE_PATH];
        if (library_read_folder(library, index, folder) &&
            library_read_string(library, folder->name, text, sizeof text) &&
            strcmp(text, name) == 0) {
            return true;
        }
    }
    return false;
}

static void a_chaptered_book_is_a_folder_of_its_chapters_and_not_a_file_row(void)
{
    static Output output;
    ScanStats stats;
    Library library;
    LibraryFolder book;
    LibraryFolder shelf;
    scan_books(&output, &stats);
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&library, (ByteSource){output_read, &output}, output.length));
    CHECK(find_folder(&library, "Chpl Book", &book));
    CHECK_EQUAL(LIBRARY_FOLDER_SHEET | LIBRARY_FOLDER_CHAPTERS, book.flags);
    CHECK_EQUAL(3u, book.track_count);
    CHECK_EQUAL(0u, book.subfolder_count);
    CHECK(find_folder(&library, "Audiobooks", &shelf));
    CHECK_EQUAL(4u, shelf.subfolder_count);
    CHECK_EQUAL(4u, shelf.track_count);
    BrowseQuery whole = {.kind = BROWSE_FOLDER_TREE, .subject = book.parent};
    CHECK_EQUAL(stats.tracks, browse_count(&library, whole));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(scanning_the_tag_fixtures_matches_the_pc_indexer),
        TEST_CASE(scanning_folders_and_playlists_matches_the_pc_indexer),
        TEST_CASE(scanning_cue_sheets_matches_the_pc_indexer),
        TEST_CASE(scanning_m4a_files_matches_the_pc_indexer),
        TEST_CASE(a_cue_sheet_makes_a_track_per_cue_track_and_hides_its_audio),
        TEST_CASE(a_favorites_file_in_the_music_root_resolves_each_cue_part),
        TEST_CASE(a_padded_favorites_file_without_directives_resolves_to_the_last_part),
        TEST_CASE(a_cue_sheet_that_cannot_be_used_leaves_the_audio_alone),
        TEST_CASE(an_m4b_with_a_chpl_atom_has_a_track_per_chapter),
        TEST_CASE(a_chpl_atom_without_the_reserved_word_is_read_too),
        TEST_CASE(an_m4b_with_a_chapter_text_track_has_a_track_per_sample),
        TEST_CASE(an_m4b_without_usable_chapters_is_one_track),
        TEST_CASE(odd_chapter_lists_are_cleaned_up),
        TEST_CASE(real_ffmpeg_chapters_are_read_from_either_style),
        TEST_CASE(a_chaptered_book_is_a_folder_of_its_chapters_and_not_a_file_row),
        TEST_CASE(a_missing_music_root_says_so),
        TEST_CASE(a_separate_listing_leaves_the_memory_to_the_builder),
        TEST_CASE(a_folder_bigger_than_the_listing_is_read_in_batches),
        TEST_CASE(many_sibling_folders_fit_a_small_listing),
        TEST_CASE(a_big_folder_nested_in_big_folders_is_read_in_batches),
        TEST_CASE(batches_build_the_same_library_as_one_whole_read),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
