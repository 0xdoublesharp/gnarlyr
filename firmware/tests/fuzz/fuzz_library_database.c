#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "library/library.h"

static const uint16_t RECORD_SIZES[LIBRARY_SECTION_COUNT] = {32, 16, 12, 12, 12, 16, 2, 12, 2, 1};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t selection(const uint8_t *data, uint32_t size, uint32_t salt)
{
    uint32_t value = salt;
    for (uint32_t index = 0; index < 4 && index < size; index++) {
        value = value * 33u ^ data[(salt + index) % size];
    }
    return value;
}

static bool same_track(const LibraryTrack *first, const LibraryTrack *second)
{
    return first->title == second->title && first->path == second->path &&
           first->album == second->album && first->artist == second->artist &&
           first->genre == second->genre && first->folder == second->folder &&
           first->duration_ms == second->duration_ms && first->disc == second->disc &&
           first->number == second->number && first->start_ms == second->start_ms &&
           first->end_ms == second->end_ms;
}

static bool same_album(const LibraryAlbum *first, const LibraryAlbum *second)
{
    return first->title == second->title && first->album_artist == second->album_artist &&
           first->genre == second->genre && first->year == second->year &&
           first->first_track == second->first_track && first->track_count == second->track_count &&
           first->cover == second->cover;
}

static bool same_group(const LibraryGroup *first, const LibraryGroup *second)
{
    return first->name == second->name && first->first_link == second->first_link &&
           first->link_count == second->link_count;
}

static bool same_folder(const LibraryFolder *first, const LibraryFolder *second)
{
    return first->name == second->name && first->parent == second->parent &&
           first->subfolder_count == second->subfolder_count &&
           first->first_link == second->first_link && first->track_count == second->track_count &&
           first->flags == second->flags;
}

static void check_sections(const Library *first, const Library *second, uint32_t size)
{
    for (int section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        LibrarySectionInfo left = first->sections[section];
        LibrarySectionInfo right = second->sections[section];
        uint64_t end = (uint64_t)left.offset + (uint64_t)left.count * left.record_size;
        fuzz_require(left.offset == right.offset && left.count == right.count);
        fuzz_require(left.record_size == right.record_size && left.music == right.music);
        fuzz_require(left.offset >= LIBRARY_HEADER_SIZE &&
                     left.record_size == RECORD_SIZES[section]);
        fuzz_require(end <= size);
        fuzz_require(library_count(first, (LibrarySection)section) == left.count);
        fuzz_require(library_music_count(first, (LibrarySection)section) <= left.count);
    }
}

static void check_track(const Library *first, const Library *second, uint32_t selected)
{
    uint32_t count = library_count(first, LIBRARY_TRACKS);
    LibraryTrack left;
    LibraryTrack right;
    fuzz_require(!library_read_track(first, count, &left));
    if (count == 0) {
        return;
    }
    uint32_t index = selected % count;
    fuzz_require(library_read_track(first, index, &left));
    fuzz_require(library_read_track(second, index, &right));
    fuzz_require(same_track(&left, &right));
}

static void check_album(const Library *first, const Library *second, uint32_t selected)
{
    uint32_t count = library_count(first, LIBRARY_ALBUMS);
    LibraryAlbum left;
    LibraryAlbum right;
    fuzz_require(!library_read_album(first, count, &left));
    if (count == 0) {
        return;
    }
    uint32_t index = selected % count;
    fuzz_require(library_read_album(first, index, &left));
    fuzz_require(library_read_album(second, index, &right));
    fuzz_require(same_album(&left, &right));
    fuzz_require(library_album_key(first, index) == library_album_key(second, index));
}

static void check_groups(const Library *first, const Library *second, uint32_t selected)
{
    static const LibrarySection SECTIONS[] = {LIBRARY_ARTISTS, LIBRARY_ALBUM_ARTISTS,
                                              LIBRARY_GENRES, LIBRARY_PLAYLISTS};
    for (size_t item = 0; item < sizeof SECTIONS / sizeof SECTIONS[0]; item++) {
        LibrarySection section = SECTIONS[item];
        uint32_t count = library_count(first, section);
        LibraryGroup left;
        LibraryGroup right;
        fuzz_require(!library_read_group(first, section, count, &left));
        if (count > 0) {
            uint32_t index = selected % count;
            fuzz_require(library_read_group(first, section, index, &left));
            fuzz_require(library_read_group(second, section, index, &right));
            fuzz_require(same_group(&left, &right));
        }
    }
}

static void check_folder(const Library *first, const Library *second, uint32_t selected)
{
    uint32_t count = library_count(first, LIBRARY_FOLDERS);
    LibraryFolder left;
    LibraryFolder right;
    fuzz_require(!library_read_folder(first, count, &left));
    if (count > 0) {
        uint32_t index = selected % count;
        fuzz_require(library_read_folder(first, index, &left));
        fuzz_require(library_read_folder(second, index, &right));
        fuzz_require(same_folder(&left, &right));
    }
}

static void check_indices(const Library *first, const Library *second, uint32_t selected)
{
    static const LibrarySection SECTIONS[] = {LIBRARY_TITLE_ORDER, LIBRARY_LINKS};
    for (size_t item = 0; item < sizeof SECTIONS / sizeof SECTIONS[0]; item++) {
        LibrarySection section = SECTIONS[item];
        uint32_t count = library_count(first, section);
        uint16_t left;
        uint16_t right;
        fuzz_require(!library_read_index(first, section, count, &left));
        if (count > 0) {
            uint32_t index = selected % count;
            fuzz_require(library_read_index(first, section, index, &left));
            fuzz_require(library_read_index(second, section, index, &right));
            fuzz_require(left == right);
        }
    }
}

static void check_string(const Library *first, const Library *second, uint32_t selected)
{
    uint32_t count = library_count(first, LIBRARY_STRINGS);
    char left[64];
    char right[64];
    uint32_t capacity = 1u + selected % sizeof left;
    fuzz_require(!library_read_string(first, count, left, capacity));
    if (count == 0) {
        return;
    }
    uint32_t offset = selected % count;
    fuzz_require(library_read_string(first, offset, left, capacity));
    fuzz_require(library_read_string(second, offset, right, capacity));
    fuzz_require(memchr(left, '\0', capacity) != NULL);
    fuzz_require(memchr(right, '\0', capacity) != NULL);
    fuzz_require(strcmp(left, right) == 0);
}

static void check_rejected(const Library *library)
{
    fuzz_require(!library->content_known && library_fingerprint(library) == 0);
    fuzz_require(library->extra == NULL);
    for (int section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        fuzz_require(library_count(library, (LibrarySection)section) == 0);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    FuzzSource first_source = {.bytes = data, .size = (uint32_t)size};
    FuzzSource second_source = first_source;
    Library first;
    Library second;
    LibraryStatus left = library_open(&first, fuzz_source(&first_source), (uint32_t)size);
    LibraryStatus right = library_open(&second, fuzz_source(&second_source), (uint32_t)size);
    fuzz_require(left >= LIBRARY_OK && left <= LIBRARY_BAD_LAYOUT);
    fuzz_require(left == right && !first_source.invalid_read && !second_source.invalid_read);
    if (left != LIBRARY_OK) {
        check_rejected(&first);
        check_rejected(&second);
        return 0;
    }
    uint32_t selected = selection(data, (uint32_t)size, library_fingerprint(&first));
    fuzz_require(first.content_known && second.content_known);
    fuzz_require(library_fingerprint(&first) == library_fingerprint(&second));
    check_sections(&first, &second, (uint32_t)size);
    check_track(&first, &second, selected);
    check_album(&first, &second, selected);
    check_groups(&first, &second, selected);
    check_folder(&first, &second, selected);
    check_indices(&first, &second, selected);
    check_string(&first, &second, selected);
    fuzz_require(!first_source.invalid_read && !second_source.invalid_read);
    return 0;
}
