#include "formal.h"
#include "library/library.h"

static void read_records(const Library *library)
{
    LibraryTrack track;
    LibraryAlbum album;
    LibraryGroup group;
    LibraryFolder folder;
    uint16_t value;
    (void)library_read_track(library, nondet_uint32(), &track);
    (void)library_read_album(library, nondet_uint32(), &album);
    (void)library_read_group(library, (LibrarySection)formal_below(LIBRARY_SECTION_COUNT),
                             nondet_uint32(), &group);
    (void)library_read_folder(library, nondet_uint32(), &folder);
    (void)library_read_index(library, (LibrarySection)formal_below(LIBRARY_SECTION_COUNT),
                             nondet_uint32(), &value);
}

static void read_text(const Library *library)
{
    char text[FORMAL_TEXT_BYTES];
    uint32_t capacity = formal_below(FORMAL_TEXT_BYTES + 1);
    if (library_read_string(library, nondet_uint32(), text, capacity) && capacity > 0) {
        formal_check_text(text, capacity);
    }
}

void harness(void)
{
    Library library;
    formal_card_fill();
    LibraryStatus status = library_open(&library, formal_card_source(), formal_card_size);
    __CPROVER_assert(status <= LIBRARY_BAD_LAYOUT, "the status is one of the five");
    if (status != LIBRARY_OK) {
        return;
    }
    for (int section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        LibrarySectionInfo info = library.sections[section];
        __CPROVER_assert((uint64_t)info.offset + (uint64_t)info.count * info.record_size <=
                             formal_card_size,
                         "every accepted section lies inside the file");
        __CPROVER_assert(library_music_count(&library, (LibrarySection)section) <=
                             library_count(&library, (LibrarySection)section),
                         "music is a part of a section");
    }
    read_records(&library);
    read_text(&library);
    (void)library_album_key(&library, nondet_uint32());
}
