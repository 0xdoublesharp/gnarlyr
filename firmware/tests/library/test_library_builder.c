#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "library/library.h"
#include "library/library_builder.h"
#include "library_compare.h"
#include "memory_source.h"
#include "random_library.h"
#include "reference_builder.h"

enum {
    PLAYER_MEMORY = 360 * 1024,
    REFERENCE_MEMORY = 64 * 1024 * 1024,
    SCRATCH_SIZE = 32 * 1024 * 1024,
    OUTPUT_CAPACITY = 16 * 1024 * 1024,
    GUARD_BYTES = 4096,
    MINUTE_MS = 60000
};

typedef struct Output {
    uint8_t *bytes;
    uint32_t length;
    int writes;
    int calls;
    int fail_write;
} Output;

typedef struct Scratch {
    uint8_t *bytes;
    uint32_t size;
    uint32_t highest;
    int reads;
    int writes;
    int fail_read;
    int fail_write;
} Scratch;

typedef struct Built {
    BuilderResult result;
    Output output;
    uint32_t length;
} Built;

static bool write_output(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Output *output = context;
    output->calls++;
    if (output->calls == output->fail_write) {
        return false;
    }
    if (offset > OUTPUT_CAPACITY || length > OUTPUT_CAPACITY - offset) {
        return false;
    }
    memcpy(output->bytes + offset, bytes, length);
    output->length = offset + length > output->length ? offset + length : output->length;
    output->writes++;
    return true;
}

static bool write_reference(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    return write_output(context, offset, bytes, length);
}

static bool scratch_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    Scratch *scratch = context;
    scratch->reads++;
    if (scratch->reads == scratch->fail_read) {
        return false;
    }
    if (offset > scratch->size || length > scratch->size - offset) {
        return false;
    }
    memcpy(buffer, scratch->bytes + offset, length);
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Scratch *scratch = context;
    scratch->writes++;
    if (scratch->writes == scratch->fail_write) {
        return false;
    }
    if (offset > scratch->size || length > scratch->size - offset) {
        return false;
    }
    memcpy(scratch->bytes + offset, bytes, length);
    scratch->highest = offset + length > scratch->highest ? offset + length : scratch->highest;
    return true;
}

static Scratch *scratch_of_size(uint32_t size)
{
    static Scratch scratch;
    free(scratch.bytes);
    scratch = (Scratch){.bytes = calloc(size, 1), .size = size, .highest = 0};
    return &scratch;
}

static BuilderScratch scratch_io(Scratch *scratch)
{
    return (BuilderScratch){scratch_read, scratch_write, scratch, scratch->size};
}

static Output fresh_output(void)
{
    return (Output){.bytes = calloc(OUTPUT_CAPACITY, 1), .length = 0, .writes = 0};
}

static BuilderTrack track(const char *path, const char *title, const char *artist,
                          const char *album_artist, const char *album, const char *genre,
                          uint16_t year, uint8_t number)
{
    return (BuilderTrack){.path = path,
                          .title = title,
                          .artist = artist,
                          .album_artist = album_artist,
                          .album = album,
                          .genre = genre,
                          .year = year,
                          .disc = 1,
                          .number = number,
                          .duration_ms = MINUTE_MS};
}

static void add_fixture_tracks(LibraryBuilder *builder)
{
    const BuilderTrack tracks[] = {
        track("AC DC/Back In Black/1-02 - Shoot to Thrill.flac", "Shoot to Thrill", "AC/DC",
              "AC/DC", "Back In Black", "Metal", 1980, 2),
        track("AC DC/Back In Black/1-01 - Hells Bells.flac", "Hells Bells", "AC/DC", "AC/DC",
              "Back In Black", "Metal", 1980, 1),
        track("AC DC/High Voltage/1-01 - Long Way.flac", "It's a Long Way to the Top", "AC/DC",
              "AC/DC", "High Voltage", "Metal", 1976, 1),
        track("Bj\xc3\xb6rk/Homogenic/02 - J\xc3\xb3ga.flac", "J\xc3\xb3ga", "Bj\xc3\xb6rk",
              "Bj\xc3\xb6rk", "Homogenic", "Electronic", 1997, 2),
        track("Bj\xc3\xb6rk/Homogenic/01 - Hunter.flac", "Hunter", "Bj\xc3\xb6rk", "Bj\xc3\xb6rk",
              "Homogenic", "Electronic", 1997, 1),
        track("Various Artists/The Best of Something/01 - Help!.flac", "Help!", "The Beatles",
              "Various Artists", "The Best of Something", "Pop", 2001, 1),
        track("Various Artists/The Best of Something/02 - Army of Me.flac", "Army of Me",
              "Bj\xc3\xb6rk", "Various Artists", "The Best of Something", "Pop", 2001, 2),
        track("acloudyskye/This Won't Be The Last Time/01 - Shoots.flac", "Shoots", "acloudyskye",
              "acloudyskye", "This Won't Be The Last Time", "Unknown Genre", 2025, 1),
    };
    for (size_t index = 0; index < sizeof tracks / sizeof tracks[0]; index++) {
        CHECK_EQUAL(BUILDER_OK, library_builder_add_track(builder, &tracks[index]));
    }
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
    uint8_t *bytes = malloc(*size + 1);
    CHECK(fread(bytes, 1, *size, file) == *size);
    fclose(file);
    bytes[*size] = 0;
    return bytes;
}

static uint8_t *player_memory(void)
{
    static uint8_t memory[PLAYER_MEMORY];
    return memory;
}

static Scratch *start(LibraryBuilder *builder)
{
    Scratch *scratch = scratch_of_size(SCRATCH_SIZE);
    library_builder_init(builder, player_memory(), PLAYER_MEMORY, scratch_io(scratch));
    return scratch;
}

static void finish(LibraryBuilder *builder, Built *built)
{
    built->output = fresh_output();
    built->length = 0;
    built->result = library_builder_finish(builder, (ByteSink){write_output, &built->output}, 0,
                                           &built->length);
}

static void check_means_fixture(const Built *built, const char *fixture)
{
    CHECK_EQUAL(BUILDER_OK, built->result);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture(fixture, &expected_size);
    CHECK_EQUAL(built->length, built->output.length);
    CHECK(library_compare(expected, expected_size, built->output.bytes, built->length));
    free(expected);
    free(built->output.bytes);
}

static void the_fixture_tracks_mean_the_same_as_the_pc_indexer(void)
{
    LibraryBuilder builder;
    start(&builder);
    add_fixture_tracks(&builder);
    Built built;
    finish(&builder, &built);
    check_means_fixture(&built, "small_library_tracks.db");
}

static char *next_field(char **cursor)
{
    char *start_of_field = *cursor;
    while (**cursor != '\t' && **cursor != '\n' && **cursor != '\0') {
        (*cursor)++;
    }
    if (**cursor != '\0') {
        *(*cursor)++ = '\0';
    }
    return start_of_field;
}

static BuilderTrack parse_track(char **cursor)
{
    BuilderTrack parsed = {.sheet = NULL};
    parsed.path = next_field(cursor);
    parsed.title = next_field(cursor);
    parsed.artist = next_field(cursor);
    parsed.album_artist = next_field(cursor);
    parsed.album = next_field(cursor);
    parsed.genre = next_field(cursor);
    parsed.year = (uint16_t)atoi(next_field(cursor));
    parsed.disc = (uint8_t)atoi(next_field(cursor));
    parsed.number = (uint8_t)atoi(next_field(cursor));
    parsed.duration_ms = (uint32_t)strtoul(next_field(cursor), NULL, 10);
    parsed.start_ms = 0;
    parsed.end_ms = 0;
    parsed.album_artist_guessed = false;
    return parsed;
}

static void add_tsv_line(LibraryBuilder *builder, char **cursor)
{
    if (strncmp(*cursor, "#PLAYLIST\t", 10) == 0) {
        *cursor += 10;
        char *name = next_field(cursor);
        char *folder = next_field(cursor);
        CHECK_EQUAL(BUILDER_OK, library_builder_begin_playlist(builder, name, folder));
    } else if (**cursor == '\t') {
        (*cursor)++;
        CHECK_EQUAL(BUILDER_OK,
                    library_builder_playlist_entry(builder, next_field(cursor), BUILDER_ANY_START));
    } else {
        BuilderTrack parsed = parse_track(cursor);
        CHECK_EQUAL(BUILDER_OK, library_builder_add_track(builder, &parsed));
    }
}

static void build_tsv(const char *tsv, const char *fixture)
{
    uint32_t size = 0;
    char *text = (char *)read_fixture(tsv, &size);
    LibraryBuilder builder;
    start(&builder);
    char *cursor = text;
    while (*cursor != '\0') {
        add_tsv_line(&builder, &cursor);
    }
    Built built;
    finish(&builder, &built);
    check_means_fixture(&built, fixture);
    free(text);
}

static void random_tracks_mean_the_same_as_the_pc_indexer(void)
{
    build_tsv("builder_random/tracks.tsv", "builder_random/library.db");
}

static void the_fixture_playlist_resolves_like_the_pc_indexer(void)
{
    LibraryBuilder builder;
    start(&builder);
    add_fixture_tracks(&builder);
    CHECK_EQUAL(BUILDER_OK, library_builder_begin_playlist(&builder, "Road Trip", "Playlists"));
    const char *entries[] = {
        "../acloudyskye/This Won't Be The Last Time/01 - Shoots.flac",
        "../AC DC/Back In Black/1-01 - Hells Bells.flac",
        "../Missing/Gone.flac",
        "../Bj\xc3\xb6rk/Homogenic/02 - J\xc3\xb3ga.flac",
    };
    for (size_t index = 0; index < sizeof entries / sizeof entries[0]; index++) {
        CHECK_EQUAL(BUILDER_OK,
                    library_builder_playlist_entry(&builder, entries[index], BUILDER_ANY_START));
    }
    Built built;
    finish(&builder, &built);
    check_means_fixture(&built, "small_library.db");
}

static void unresolved_playlist_paths_cover_every_leading_slash_form(void)
{
    LibraryBuilder builder;
    start(&builder);
    add_fixture_tracks(&builder);
    CHECK_EQUAL(BUILDER_OK, library_builder_begin_playlist(&builder, "Odd paths", "Playlists"));
    static const char *entries[] = {
        "", "/", "//", "//missing.flac", "///missing.flac", "missing.flac"};
    for (uint32_t index = 0; index < sizeof entries / sizeof entries[0]; index++) {
        CHECK_EQUAL(BUILDER_OK,
                    library_builder_playlist_entry(&builder, entries[index], BUILDER_ANY_START));
    }
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    MemorySource memory = {.bytes = built.output.bytes, .size = built.length};
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, memory_source(&memory), built.length));
    LibraryGroup playlist;
    CHECK(library_read_group(&library, LIBRARY_PLAYLISTS, 0, &playlist));
    CHECK_EQUAL(0, playlist.link_count);
    free(built.output.bytes);
}

static BuilderTrack part_of(const char *title, uint8_t number, uint32_t start, uint32_t end)
{
    BuilderTrack part =
        track("Cue/Album/Album.flac", title, "Band", "Band", "Album", "Rock", 1990, number);
    part.start_ms = start;
    part.end_ms = end;
    return part;
}

static uint16_t playlist_link(const Library *library, uint32_t index)
{
    LibraryGroup group;
    uint16_t link = 0;
    CHECK(library_read_group(library, LIBRARY_PLAYLISTS, 0, &group));
    CHECK(index < group.link_count);
    CHECK(library_read_index(library, LIBRARY_LINKS, group.first_link + index, &link));
    return link;
}

static void a_start_in_a_playlist_entry_picks_the_cue_part(void)
{
    LibraryBuilder builder;
    start(&builder);
    BuilderTrack tracks[] = {part_of("One", 1, 0, 1000), part_of("Two", 2, 1000, 2493),
                             part_of("Three", 3, 2493, 0)};
    for (size_t index = 0; index < 3; index++) {
        CHECK_EQUAL(BUILDER_OK, library_builder_add_track(&builder, &tracks[index]));
    }
    CHECK_EQUAL(BUILDER_OK, library_builder_begin_playlist(&builder, "Mix", ""));
    static const char PATH[] = "cue/album/ALBUM.flac";
    static const uint32_t STARTS[] = {1000, 0, 2493, BUILDER_ANY_START, 777};
    for (size_t index = 0; index < sizeof STARTS / sizeof STARTS[0]; index++) {
        CHECK_EQUAL(BUILDER_OK, library_builder_playlist_entry(&builder, PATH, STARTS[index]));
    }
    CHECK_EQUAL(BUILDER_OK, library_builder_playlist_entry(&builder, "Cue/Missing.flac", 1000));
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    MemorySource memory = {.bytes = built.output.bytes, .size = built.length};
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, memory_source(&memory), built.length));
    LibraryGroup group;
    CHECK(library_read_group(&library, LIBRARY_PLAYLISTS, 0, &group));
    CHECK_EQUAL(5u, group.link_count);
    char title[16];
    static const char *const WANTED[] = {"Two", "One", "Three", "Three", "Three"};
    for (uint32_t index = 0; index < 5; index++) {
        LibraryTrack found;
        CHECK(library_read_track(&library, playlist_link(&library, index), &found));
        CHECK(library_read_string(&library, found.title, title, sizeof title));
        CHECK_EQUAL(0, strcmp(WANTED[index], title));
    }
    free(built.output.bytes);
}

static void random_playlists_resolve_like_the_pc_indexer(void)
{
    build_tsv("builder_random/with_playlists.tsv", "builder_random/with_playlists.db");
}

static ReferenceTrack reference_track(const BuilderTrack *track)
{
    return (ReferenceTrack){.path = track->path,
                            .title = track->title,
                            .artist = track->artist,
                            .album_artist = track->album_artist,
                            .album = track->album,
                            .genre = track->genre,
                            .year = track->year,
                            .disc = track->disc,
                            .number = track->number,
                            .duration_ms = track->duration_ms,
                            .album_artist_guessed = track->album_artist_guessed};
}

static void build_reference(const RandomLibrary *library, Built *built)
{
    uint8_t *memory = malloc(REFERENCE_MEMORY);
    ReferenceBuilder builder;
    reference_builder_init(&builder, memory, REFERENCE_MEMORY);
    for (uint32_t index = 0; index < library->track_count; index++) {
        ReferenceTrack added = reference_track(&library->tracks[index]);
        CHECK_EQUAL(REFERENCE_OK, reference_builder_add_track(&builder, &added));
    }
    for (uint32_t index = 0; index < library->playlist_count; index++) {
        const RandomPlaylist *playlist = &library->playlists[index];
        CHECK_EQUAL(REFERENCE_OK,
                    reference_builder_begin_playlist(&builder, playlist->name, playlist->folder));
        for (uint32_t entry = 0; entry < playlist->count; entry++) {
            CHECK_EQUAL(REFERENCE_OK,
                        reference_builder_playlist_entry(&builder, playlist->entries[entry]));
        }
    }
    built->output = fresh_output();
    built->length = 0;
    CHECK_EQUAL(REFERENCE_OK,
                reference_builder_finish(&builder, (ReferenceSink){write_reference, &built->output},
                                         0, &built->length));
    free(memory);
}

static BuilderResult add_library(LibraryBuilder *builder, const RandomLibrary *library)
{
    for (uint32_t index = 0; index < library->track_count; index++) {
        BuilderResult result = library_builder_add_track(builder, &library->tracks[index]);
        if (result != BUILDER_OK) {
            return result;
        }
    }
    for (uint32_t index = 0; index < library->playlist_count; index++) {
        const RandomPlaylist *playlist = &library->playlists[index];
        BuilderResult result =
            library_builder_begin_playlist(builder, playlist->name, playlist->folder);
        for (uint32_t entry = 0; result == BUILDER_OK && entry < playlist->count; entry++) {
            result = library_builder_playlist_entry(builder, playlist->entries[entry],
                                                    BUILDER_ANY_START);
        }
        if (result != BUILDER_OK) {
            return result;
        }
    }
    return BUILDER_OK;
}

static void check_matches_reference(uint32_t tracks, uint32_t playlists, uint32_t seed)
{
    RandomLibrary library;
    random_library_make(&library, tracks, playlists, seed);
    Built expected;
    build_reference(&library, &expected);
    LibraryBuilder builder;
    start(&builder);
    CHECK_EQUAL(BUILDER_OK, add_library(&builder, &library));
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    CHECK(
        library_compare(expected.output.bytes, expected.length, built.output.bytes, built.length));
    free(expected.output.bytes);
    free(built.output.bytes);
    random_library_free(&library);
}

static void random_libraries_match_the_reference_builder(void)
{
    check_matches_reference(1, 1, 1);
    check_matches_reference(60, 3, 2);
    check_matches_reference(900, 12, 3);
    check_matches_reference(3000, 20, 4);
}

static void a_20000_track_library_builds_in_the_players_memory(void)
{
    check_matches_reference(20000, 30, 5);
}

static void the_builder_stays_inside_its_memory(void)
{
    static uint8_t block[PLAYER_MEMORY + 2 * GUARD_BYTES];
    memset(block, 0xA5, sizeof block);
    RandomLibrary library;
    random_library_make(&library, 3000, 10, 6);
    LibraryBuilder builder;
    library_builder_init(&builder, block + GUARD_BYTES, PLAYER_MEMORY,
                         scratch_io(scratch_of_size(SCRATCH_SIZE)));
    CHECK_EQUAL(BUILDER_OK, add_library(&builder, &library));
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    bool untouched = true;
    for (uint32_t index = 0; index < GUARD_BYTES; index++) {
        untouched =
            untouched && block[index] == 0xA5 && block[GUARD_BYTES + PLAYER_MEMORY + index] == 0xA5;
    }
    CHECK(untouched);
    free(built.output.bytes);
    random_library_free(&library);
}

static void the_path_index_is_built_at_the_first_playlist_in_scan_order(void)
{
    enum {
        TRACKS = 3000
    };
    RandomLibrary library;
    random_library_make(&library, TRACKS, 0, 9);
    LibraryBuilder builder;
    Scratch *scratch = start(&builder);
    CHECK_EQUAL(BUILDER_OK, add_library(&builder, &library));
    CHECK_EQUAL(BUILDER_OK, library_builder_prepare(&builder));
    int prepared = scratch->reads;
    CHECK_EQUAL(BUILDER_OK, library_builder_begin_playlist(&builder, "Mix", ""));
    int indexed = scratch->reads - prepared;
    CHECK(indexed > 0);
    CHECK(indexed < TRACKS / 2);
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    free(built.output.bytes);
    random_library_free(&library);
}

static void a_scratch_file_too_small_says_there_is_no_room(void)
{
    RandomLibrary library;
    random_library_make(&library, 3000, 0, 7);
    LibraryBuilder builder;
    library_builder_init(&builder, player_memory(), PLAYER_MEMORY,
                         scratch_io(scratch_of_size(64 * 1024)));
    BuilderResult result = add_library(&builder, &library);
    if (result == BUILDER_OK) {
        Built built;
        finish(&builder, &built);
        result = built.result;
        free(built.output.bytes);
    }
    CHECK_EQUAL(BUILDER_NO_ROOM, result);
    random_library_free(&library);
}

static void too_little_memory_says_so(void)
{
    static uint8_t memory[2048];
    LibraryBuilder builder;
    library_builder_init(&builder, memory, sizeof memory,
                         scratch_io(scratch_of_size(SCRATCH_SIZE)));
    BuilderTrack added =
        track("Artist/Album/01.flac", "One", "Artist", "Artist", "Album", "Rock", 2000, 1);
    BuilderResult result = library_builder_add_track(&builder, &added);
    if (result == BUILDER_OK) {
        Built built;
        finish(&builder, &built);
        result = built.result;
        free(built.output.bytes);
    }
    CHECK_EQUAL(BUILDER_OUT_OF_MEMORY, result);
}

static void intermediate_memory_limits_are_bounded_and_never_publish_partial_databases(void)
{
    static uint8_t memory[PLAYER_MEMORY + 2 * GUARD_BYTES];
    Scratch *scratch = scratch_of_size(SCRATCH_SIZE);
    uint32_t successes = 0;
    uint32_t exhausted = 0;
    for (uint32_t size = 2048; size <= PLAYER_MEMORY; size += 2048) {
        memset(memory, 0xa5, sizeof memory);
        LibraryBuilder builder;
        library_builder_init(&builder, memory + GUARD_BYTES, size, scratch_io(scratch));
        BuilderTrack added =
            track("Artist/Album/01.flac", "One", "Artist", "Artist", "Album", "Rock", 2000, 1);
        BuilderResult result = library_builder_add_track(&builder, &added);
        if (result == BUILDER_OK) {
            Built built;
            finish(&builder, &built);
            result = built.result;
            if (result == BUILDER_OK) {
                MemorySource source = {.bytes = built.output.bytes, .size = built.length};
                Library library;
                CHECK_EQUAL(LIBRARY_OK,
                            library_open(&library, memory_source(&source), built.length));
                CHECK_EQUAL(1u, library_count(&library, LIBRARY_TRACKS));
            } else {
                CHECK_EQUAL(0u, built.length);
                CHECK(memcmp(built.output.bytes, "DSDB", 4) != 0);
            }
            free(built.output.bytes);
        }
        CHECK(result == BUILDER_OK || result == BUILDER_OUT_OF_MEMORY);
        successes += result == BUILDER_OK;
        exhausted += result == BUILDER_OUT_OF_MEMORY;
        for (uint32_t index = 0; index < GUARD_BYTES; index++) {
            CHECK_EQUAL(0xa5, memory[index]);
            CHECK_EQUAL(0xa5, memory[GUARD_BYTES + size + index]);
        }
    }
    CHECK(successes > 0);
    CHECK(exhausted > 0);
}

static void the_65536th_track_is_too_many(void)
{
    LibraryBuilder builder;
    start(&builder);
    char path[64];
    BuilderResult result = BUILDER_OK;
    uint32_t added = 0;
    for (; added < 65536 && result == BUILDER_OK; added++) {
        snprintf(path, sizeof path, "A/%05u.flac", added);
        BuilderTrack next = track(path, "T", "A", "A", "B", "G", 2000, 1);
        result = library_builder_add_track(&builder, &next);
    }
    CHECK_EQUAL(BUILDER_TOO_MANY, result);
    CHECK_EQUAL(65536u, added);
}

static void a_library_bigger_than_its_room_writes_nothing(void)
{
    LibraryBuilder builder;
    start(&builder);
    add_fixture_tracks(&builder);
    Output output = fresh_output();
    uint32_t length = 0;
    CHECK_EQUAL(BUILDER_NO_ROOM,
                library_builder_finish(&builder, (ByteSink){write_output, &output}, 1000, &length));
    CHECK_EQUAL(0, output.writes);
    free(output.bytes);
}

static void an_empty_library_still_writes_a_valid_header(void)
{
    LibraryBuilder builder;
    start(&builder);
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    CHECK(built.length >= 128);
    CHECK_EQUAL(0, memcmp("DSDB", built.output.bytes, 4));
    free(built.output.bytes);
}

static BuilderTrack guessed(const char *path, const char *artist, const char *album, uint8_t number)
{
    BuilderTrack added = track(path, "Song", artist, artist, album, "Pop", 2025, number);
    added.album_artist_guessed = true;
    return added;
}

typedef struct Opened {
    Built built;
    MemorySource memory;
    Library library;
} Opened;

static void build_tracks(const BuilderTrack *tracks, size_t count, Opened *opened)
{
    LibraryBuilder builder;
    start(&builder);
    for (size_t index = 0; index < count; index++) {
        CHECK_EQUAL(BUILDER_OK, library_builder_add_track(&builder, &tracks[index]));
    }
    finish(&builder, &opened->built);
    CHECK_EQUAL(BUILDER_OK, opened->built.result);
    opened->memory =
        (MemorySource){.bytes = opened->built.output.bytes, .size = opened->built.length};
    CHECK_EQUAL(LIBRARY_OK, library_open(&opened->library, memory_source(&opened->memory),
                                         opened->built.length));
}

static bool album_is(const Opened *opened, uint32_t index, const char *artist, uint16_t tracks)
{
    LibraryAlbum album;
    LibraryGroup group;
    char name[64];
    return library_read_album(&opened->library, index, &album) &&
           library_read_group(&opened->library, LIBRARY_ALBUM_ARTISTS, album.album_artist,
                              &group) &&
           library_read_string(&opened->library, group.name, name, sizeof name) &&
           strcmp(artist, name) == 0 && album.track_count == tracks;
}

static void a_featured_artist_stays_with_the_album(void)
{
    const BuilderTrack tracks[] = {
        guessed("The Weeknd/Hurry Up Tomorrow/01.flac", "The Weeknd & Justice", "Hurry Up Tomorrow",
                1),
        guessed("The Weeknd/Hurry Up Tomorrow/02.flac", "The Weeknd", "Hurry Up Tomorrow", 2),
        guessed("The Weeknd/Hurry Up Tomorrow/03.flac", "The Weeknd", "Hurry Up Tomorrow", 3),
    };
    Opened opened;
    build_tracks(tracks, sizeof tracks / sizeof tracks[0], &opened);
    CHECK_EQUAL(1, library_count(&opened.library, LIBRARY_ALBUMS));
    CHECK(album_is(&opened, 0, "The Weeknd", 3));
    CHECK_EQUAL(1, library_count(&opened.library, LIBRARY_ALBUM_ARTISTS));
    CHECK_EQUAL(2, library_count(&opened.library, LIBRARY_ARTISTS));
    free(opened.built.output.bytes);
}

static void a_tie_goes_to_the_artist_first_in_order(void)
{
    const BuilderTrack tracks[] = {
        guessed("Duo/Split/01.flac", "Second", "Split", 1),
        guessed("Duo/Split/02.flac", "First", "Split", 2),
    };
    Opened opened;
    build_tracks(tracks, sizeof tracks / sizeof tracks[0], &opened);
    CHECK_EQUAL(1, library_count(&opened.library, LIBRARY_ALBUMS));
    CHECK(album_is(&opened, 0, "First", 2));
    free(opened.built.output.bytes);
}

static void tagged_album_artists_and_other_folders_keep_their_albums(void)
{
    const BuilderTrack tracks[] = {
        guessed("A/Same Name/01.flac", "Alpha", "Same Name", 1),
        guessed("B/Same Name/01.flac", "Beta", "Same Name", 1),
        track("C/Mix/01.flac", "One", "Gamma", "Various Artists", "Mix", "Pop", 2025, 1),
        guessed("C/Mix/02.flac", "Delta", "Mix", 2),
        guessed("D/Discs/CD1/01.flac", "Echo", "Discs", 1),
        guessed("D/Discs/CD2/01.flac", "Echo", "Discs", 1),
    };
    Opened opened;
    build_tracks(tracks, sizeof tracks / sizeof tracks[0], &opened);
    CHECK_EQUAL(5, library_count(&opened.library, LIBRARY_ALBUMS));
    free(opened.built.output.bytes);
}

static void check_preparation_fault(bool read, int operation)
{
    LibraryBuilder builder;
    Scratch *scratch = start(&builder);
    add_fixture_tracks(&builder);
    if (read) {
        scratch->fail_read = scratch->reads + operation;
    } else {
        scratch->fail_write = scratch->writes + operation;
    }
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_WRITE_FAILED, built.result);
    CHECK_EQUAL(0, built.output.calls);
    CHECK_EQUAL(0, built.length);
    CHECK_EQUAL(BUILDER_WRITE_FAILED, library_builder_prepare(&builder));
    free(built.output.bytes);
}

static void failed_scratch_reads_during_preparation_never_publish_a_database(void)
{
    check_preparation_fault(true, 1);
    check_preparation_fault(true, 2);
    check_preparation_fault(true, 8);
}

static void failed_scratch_writes_during_preparation_never_publish_a_database(void)
{
    check_preparation_fault(false, 1);
    check_preparation_fault(false, 2);
    check_preparation_fault(false, 8);
}

static void a_scratch_read_failure_after_preparation_is_an_io_error(void)
{
    LibraryBuilder builder;
    Scratch *scratch = start(&builder);
    add_fixture_tracks(&builder);
    CHECK_EQUAL(BUILDER_OK, library_builder_prepare(&builder));
    scratch->fail_read = scratch->reads + 1;
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_WRITE_FAILED, built.result);
    CHECK_EQUAL(0, built.length);
    CHECK_EQUAL(0, built.output.calls);
    free(built.output.bytes);
}

static Scratch *prepare_for_encoding(LibraryBuilder *builder)
{
    Scratch *scratch = start(builder);
    add_fixture_tracks(builder);
    CHECK_EQUAL(BUILDER_OK, library_builder_begin_playlist(builder, "Road Trip", "Playlists"));
    CHECK_EQUAL(BUILDER_OK,
                library_builder_playlist_entry(
                    builder, "../AC DC/Back In Black/1-01 - Hells Bells.flac", BUILDER_ANY_START));
    return scratch;
}

static void check_encoding_faults(bool read)
{
    LibraryBuilder reference;
    Scratch *scratch = prepare_for_encoding(&reference);
    int before = read ? scratch->reads : scratch->writes;
    Built built;
    finish(&reference, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    int operations = (read ? scratch->reads : scratch->writes) - before;
    CHECK(operations > 0);
    free(built.output.bytes);
    int checkpoints = operations < 128 ? operations : 128;
    for (int part = 0; part < checkpoints; part++) {
        LibraryBuilder builder;
        scratch = prepare_for_encoding(&builder);
        int failure = checkpoints > 1 ? 1 + (operations - 1) * part / (checkpoints - 1) : 1;
        if (read) {
            scratch->fail_read = scratch->reads + failure;
        } else {
            scratch->fail_write = scratch->writes + failure;
        }
        finish(&builder, &built);
        CHECK_EQUAL(BUILDER_WRITE_FAILED, built.result);
        CHECK_EQUAL(0u, built.length);
        CHECK(memcmp(built.output.bytes, "DSDB", 4) != 0);
        free(built.output.bytes);
    }
}

static void scratch_read_failures_during_encoding_do_not_publish_a_database(void)
{
    check_encoding_faults(true);
}

static void scratch_write_failures_during_encoding_do_not_publish_a_database(void)
{
    check_encoding_faults(false);
}

static void every_destination_failure_leaves_the_database_unpublished(void)
{
    LibraryBuilder builder;
    start(&builder);
    add_fixture_tracks(&builder);
    Built reference;
    finish(&builder, &reference);
    CHECK_EQUAL(BUILDER_OK, reference.result);
    CHECK(reference.output.calls >= 2);
    for (int failure = 1; failure <= reference.output.calls; failure++) {
        Output output = fresh_output();
        output.fail_write = failure;
        memset(output.bytes, 0xA5, OUTPUT_CAPACITY);
        uint32_t length = 0;
        CHECK_EQUAL(
            BUILDER_WRITE_FAILED,
            library_builder_finish(&builder, (ByteSink){write_output, &output}, 0, &length));
        CHECK_EQUAL(failure, output.calls);
        CHECK_EQUAL(0, length);
        CHECK(memcmp(output.bytes, "DSDB", 4) != 0);
        free(output.bytes);
    }
    free(reference.output.bytes);
}

static void a_failed_destination_can_be_retried_without_losing_tracks(void)
{
    LibraryBuilder builder;
    start(&builder);
    add_fixture_tracks(&builder);
    Output failed = fresh_output();
    failed.fail_write = 1;
    uint32_t length = 0;
    CHECK_EQUAL(BUILDER_WRITE_FAILED,
                library_builder_finish(&builder, (ByteSink){write_output, &failed}, 0, &length));
    free(failed.bytes);
    Built built;
    finish(&builder, &built);
    CHECK_EQUAL(BUILDER_OK, built.result);
    MemorySource source = {.bytes = built.output.bytes, .size = built.length};
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, memory_source(&source), built.length));
    CHECK_EQUAL(8, library_count(&library, LIBRARY_TRACKS));
    free(built.output.bytes);
}

static void check_album_metadata(const Opened *opened, uint16_t year, const char *genre)
{
    LibraryAlbum album;
    LibraryGroup group;
    char name[64];
    CHECK(library_read_album(&opened->library, 0, &album));
    CHECK_EQUAL(year, album.year);
    CHECK(library_read_group(&opened->library, LIBRARY_GENRES, album.genre, &group));
    CHECK(library_read_string(&opened->library, group.name, name, sizeof name));
    CHECK_EQUAL(0, strcmp(genre, name));
}

static void the_most_frequent_genre_represents_a_mixed_album(void)
{
    const BuilderTrack tracks[] = {
        track("A/B/01.flac", "One", "A", "A", "B", "Ambient", 2000, 1),
        track("A/B/02.flac", "Two", "A", "A", "B", "Rock", 2000, 2),
        track("A/B/03.flac", "Three", "A", "A", "B", "Rock", 2000, 3),
    };
    Opened opened;
    build_tracks(tracks, 3, &opened);
    check_album_metadata(&opened, 2000, "Rock");
    free(opened.built.output.bytes);
}

static void a_genre_tie_uses_input_order_before_track_sorting(void)
{
    const BuilderTrack tracks[] = {
        track("A/B/02.flac", "Zulu", "A", "A", "B", "Rock", 2000, 2),
        track("A/B/01.flac", "Alpha", "A", "A", "B", "Ambient", 2000, 1),
    };
    Opened opened;
    build_tracks(tracks, 2, &opened);
    check_album_metadata(&opened, 2000, "Rock");
    LibraryTrack first;
    CHECK(library_read_track(&opened.library, 0, &first));
    CHECK_EQUAL(1, first.number);
    free(opened.built.output.bytes);
}

static void mixed_disc_years_ignore_missing_dates_and_keep_disc_order(void)
{
    BuilderTrack tracks[] = {
        track("A/B/03.flac", "Three", "A", "A", "B", "Rock", 2003, 1),
        track("A/B/02.flac", "Two", "A", "A", "B", "Rock", 0, 2),
        track("A/B/01.flac", "One", "A", "A", "B", "Rock", 1998, 1),
    };
    tracks[0].disc = 2;
    Opened opened;
    build_tracks(tracks, 3, &opened);
    check_album_metadata(&opened, 1998, "Rock");
    for (uint32_t index = 0; index < 3; index++) {
        LibraryTrack item;
        CHECK(library_read_track(&opened.library, index, &item));
        CHECK_EQUAL(index == 2 ? 2 : 1, item.disc);
        CHECK_EQUAL(index == 1 ? 2 : 1, item.number);
    }
    free(opened.built.output.bytes);
}

static void compilation_groups_link_each_album_only_once(void)
{
    const BuilderTrack tracks[] = {
        track("Mix/01.flac", "One", "Alpha", "Various", "Mix", "Rock", 2000, 1),
        track("Mix/02.flac", "Two", "Beta", "Various", "Mix", "Jazz", 2000, 2),
        track("Mix/03.flac", "Three", "Alpha", "Various", "Mix", "Rock", 2000, 3),
        track("Mix/04.flac", "Four", "Beta", "Various", "Mix", "Jazz", 2000, 4),
    };
    Opened opened;
    build_tracks(tracks, 4, &opened);
    CHECK_EQUAL(1, library_count(&opened.library, LIBRARY_ALBUMS));
    CHECK(album_is(&opened, 0, "Various", 4));
    const LibrarySection sections[] = {LIBRARY_ARTISTS, LIBRARY_GENRES};
    for (size_t section = 0; section < 2; section++) {
        CHECK_EQUAL(2, library_count(&opened.library, sections[section]));
        for (uint32_t index = 0; index < 2; index++) {
            LibraryGroup group;
            uint16_t album = LIBRARY_NONE;
            CHECK(library_read_group(&opened.library, sections[section], index, &group));
            CHECK_EQUAL(1, group.link_count);
            CHECK(library_read_index(&opened.library, LIBRARY_LINKS, group.first_link, &album));
            CHECK_EQUAL(0, album);
        }
    }
    free(opened.built.output.bytes);
}

static void identical_music_and_audiobook_tags_keep_separate_albums(void)
{
    const BuilderTrack tracks[] = {
        track("Audiobooks/Shared/01.flac", "One", "A", "A", "Shared", "G", 2000, 1),
        track("Music/Shared/01.flac", "One", "A", "A", "Shared", "G", 2000, 1),
    };
    Opened opened;
    build_tracks(tracks, 2, &opened);
    CHECK_EQUAL(2, library_count(&opened.library, LIBRARY_ALBUMS));
    CHECK_EQUAL(1, library_music_count(&opened.library, LIBRARY_ALBUMS));
    CHECK_EQUAL(2, library_count(&opened.library, LIBRARY_TRACKS));
    CHECK_EQUAL(1, library_music_count(&opened.library, LIBRARY_TRACKS));
    free(opened.built.output.bytes);
}

static void empty_tags_remain_readable_strings(void)
{
    BuilderTrack item = track("Music/01.flac", "", "", "", "", "", 0, 0);
    Opened opened;
    build_tracks(&item, 1, &opened);
    LibraryTrack result;
    LibraryAlbum album;
    char text[8] = "stale";
    CHECK(library_read_track(&opened.library, 0, &result));
    CHECK(library_read_string(&opened.library, result.title, text, sizeof text));
    CHECK_EQUAL(0, strcmp("", text));
    CHECK(library_read_album(&opened.library, result.album, &album));
    CHECK(library_read_string(&opened.library, album.title, text, sizeof text));
    CHECK_EQUAL(0, strcmp("", text));
    check_album_metadata(&opened, 0, "");
    free(opened.built.output.bytes);
}

static void a_large_mixed_album_keeps_votes_and_order_across_sort_runs(void)
{
    LibraryBuilder builder;
    start(&builder);
    for (uint32_t index = 0; index < 20000; index++) {
        char path[64];
        char title[32];
        snprintf(path, sizeof path, "Music/Large/%05u.flac", index);
        snprintf(title, sizeof title, "T%05u", index);
        BuilderTrack item = track(path, title, "A", "A", "Large", index % 4 ? "Jazz" : "Rock",
                                  (uint16_t)(1990 + index % 20), (uint8_t)(index % 17 + 1));
        item.disc = (uint8_t)(index % 2 + 1);
        CHECK_EQUAL(BUILDER_OK, library_builder_add_track(&builder, &item));
    }
    Opened opened;
    finish(&builder, &opened.built);
    CHECK_EQUAL(BUILDER_OK, opened.built.result);
    opened.memory = (MemorySource){.bytes = opened.built.output.bytes, .size = opened.built.length};
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&opened.library, memory_source(&opened.memory), opened.built.length));
    CHECK_EQUAL(1, library_count(&opened.library, LIBRARY_ALBUMS));
    CHECK_EQUAL(20000, library_count(&opened.library, LIBRARY_TRACKS));
    check_album_metadata(&opened, 1990, "Jazz");
    LibraryTrack previous = {0};
    char previous_title[32] = "";
    for (uint32_t index = 0; index < 20000; index++) {
        LibraryTrack item;
        char title[32];
        CHECK(library_read_track(&opened.library, index, &item));
        CHECK(library_read_string(&opened.library, item.title, title, sizeof title));
        if (index != 0) {
            CHECK(previous.disc < item.disc ||
                  (previous.disc == item.disc &&
                   (previous.number < item.number ||
                    (previous.number == item.number && strcmp(previous_title, title) < 0))));
        }
        previous = item;
        memcpy(previous_title, title, sizeof title);
    }
    free(opened.built.output.bytes);
}

static void check_preparation_checkpoints(bool read)
{
    LibraryBuilder builder;
    Scratch *scratch = start(&builder);
    add_fixture_tracks(&builder);
    int before = read ? scratch->reads : scratch->writes;
    CHECK_EQUAL(BUILDER_OK, library_builder_prepare(&builder));
    int operations = (read ? scratch->reads : scratch->writes) - before;
    CHECK(operations >= 4);
    for (int part = 0; part < 256; part++) {
        check_preparation_fault(read, 1 + (operations - 1) * part / 255);
    }
}

static void scratch_reads_fail_safely_through_the_whole_preparation(void)
{
    check_preparation_checkpoints(true);
}

static void scratch_writes_fail_safely_through_the_whole_preparation(void)
{
    check_preparation_checkpoints(false);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_fixture_tracks_mean_the_same_as_the_pc_indexer),
        TEST_CASE(random_tracks_mean_the_same_as_the_pc_indexer),
        TEST_CASE(the_fixture_playlist_resolves_like_the_pc_indexer),
        TEST_CASE(unresolved_playlist_paths_cover_every_leading_slash_form),
        TEST_CASE(random_playlists_resolve_like_the_pc_indexer),
        TEST_CASE(a_start_in_a_playlist_entry_picks_the_cue_part),
        TEST_CASE(random_libraries_match_the_reference_builder),
        TEST_CASE(a_20000_track_library_builds_in_the_players_memory),
        TEST_CASE(the_builder_stays_inside_its_memory),
        TEST_CASE(the_path_index_is_built_at_the_first_playlist_in_scan_order),
        TEST_CASE(a_scratch_file_too_small_says_there_is_no_room),
        TEST_CASE(too_little_memory_says_so),
        TEST_CASE(intermediate_memory_limits_are_bounded_and_never_publish_partial_databases),
        TEST_CASE(the_65536th_track_is_too_many),
        TEST_CASE(a_library_bigger_than_its_room_writes_nothing),
        TEST_CASE(an_empty_library_still_writes_a_valid_header),
        TEST_CASE(a_featured_artist_stays_with_the_album),
        TEST_CASE(a_tie_goes_to_the_artist_first_in_order),
        TEST_CASE(tagged_album_artists_and_other_folders_keep_their_albums),
        TEST_CASE(failed_scratch_reads_during_preparation_never_publish_a_database),
        TEST_CASE(failed_scratch_writes_during_preparation_never_publish_a_database),
        TEST_CASE(a_scratch_read_failure_after_preparation_is_an_io_error),
        TEST_CASE(scratch_read_failures_during_encoding_do_not_publish_a_database),
        TEST_CASE(scratch_write_failures_during_encoding_do_not_publish_a_database),
        TEST_CASE(every_destination_failure_leaves_the_database_unpublished),
        TEST_CASE(a_failed_destination_can_be_retried_without_losing_tracks),
        TEST_CASE(the_most_frequent_genre_represents_a_mixed_album),
        TEST_CASE(a_genre_tie_uses_input_order_before_track_sorting),
        TEST_CASE(mixed_disc_years_ignore_missing_dates_and_keep_disc_order),
        TEST_CASE(compilation_groups_link_each_album_only_once),
        TEST_CASE(identical_music_and_audiobook_tags_keep_separate_albums),
        TEST_CASE(empty_tags_remain_readable_strings),
        TEST_CASE(a_large_mixed_album_keeps_votes_and_order_across_sort_runs),
        TEST_CASE(scratch_reads_fail_safely_through_the_whole_preparation),
        TEST_CASE(scratch_writes_fail_safely_through_the_whole_preparation),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    free(scratch_of_size(1)->bytes);
    return result;
}
