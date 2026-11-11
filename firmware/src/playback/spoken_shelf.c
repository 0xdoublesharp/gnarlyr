#include "playback/spoken_shelf.h"

#include "core/seam.h"
#include "playback/spoken.h"

enum {
    NAME_CAPACITY = 64,
    PATH_CAPACITY = 520
};

void spoken_shelf_init(SpokenShelf *shelf, SpokenKind kind, void (*between)(void *context),
                       void *context)
{
    *shelf = (SpokenShelf){.kind = kind, .between = between, .context = context};
}

static void service(const SpokenShelf *shelf)
{
    if (shelf->between != NULL) {
        shelf->between(shelf->context);
    }
}

static bool root_matches(const Library *library, const SpokenShelf *shelf, uint16_t index)
{
    LibraryFolder folder;
    char name[NAME_CAPACITY];
    if (!library_read_folder(library, index, &folder) ||
        !library_read_string(library, folder.name, name, sizeof name)) {
        return false;
    }
    size_t length = 0;
    while (name[length] != '\0') {
        length++;
    }
    return spoken_kind_of_name(name, length) == shelf->kind;
}

static bool named_next(const Library *library, SpokenShelf *shelf, BrowseItem *item)
{
    LibraryFolder root;
    if (!library_read_folder(library, 0, &root)) {
        return false;
    }
    while (shelf->root < root.subfolder_count) {
        service(shelf);
        uint16_t index = 0;
        BrowseQuery query = browse_root_query(BROWSE_FOLDER);
        if (library_read_index(library, LIBRARY_LINKS, root.first_link + shelf->root, &index) &&
            root_matches(library, shelf, index)) {
            query.subject = index;
            if (shelf->child < browse_count(library, query)) {
                uint32_t position = shelf->child++;
                return item == NULL || browse_item(library, query, position, item);
            }
        }
        shelf->root++;
        shelf->child = 0;
    }
    return false;
}

static bool book_folder(const Library *library, uint16_t track, uint16_t parent, BrowseItem *item)
{
    LibraryFolder folder;
    if (!library_read_folder(library, parent, &folder)) {
        return false;
    }
    for (uint32_t index = 0; index < folder.subfolder_count; index++) {
        uint16_t child = 0;
        uint16_t first = 0;
        LibraryFolder book;
        if (library_read_index(library, LIBRARY_LINKS, folder.first_link + index, &child) &&
            library_read_folder(library, child, &book) &&
            (book.flags & LIBRARY_FOLDER_CHAPTERS) != 0 && book.track_count > 0 &&
            library_read_index(library, LIBRARY_LINKS, book.first_link + book.subfolder_count,
                               &first) &&
            first == track) {
            *item = (BrowseItem){.kind = BROWSE_ITEM_FOLDER, .index = child};
            return true;
        }
    }
    return false;
}

static bool extra_next(const Library *library, SpokenShelf *shelf, BrowseItem *item)
{
    uint32_t music = library_music_count(library, LIBRARY_TRACKS);
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    if (shelf->kind != SPOKEN_AUDIOBOOK) {
        return false;
    }
    while (shelf->track < count - music) {
        service(shelf);
        uint16_t index = (uint16_t)(music + shelf->track++);
        LibraryTrack track;
        char path[PATH_CAPACITY];
        if (!library_read_track(library, index, &track) ||
            !library_read_string(library, track.path, path, sizeof path) ||
            spoken_kind_of_folder(path) != SPOKEN_NONE ||
            spoken_kind_of(path) != SPOKEN_AUDIOBOOK) {
            continue;
        }
        bool chapter = track.start_ms != 0 || track.end_ms != 0;
        if (chapter && track.number != 1) {
            continue;
        }
        if (item == NULL) {
            return true;
        }
        if (chapter) {
            return book_folder(library, index, track.folder, item);
        }
        *item = (BrowseItem){.kind = BROWSE_ITEM_TRACK, .index = index};
        return true;
    }
    return false;
}

static bool next(const Library *library, SpokenShelf *shelf, BrowseItem *item)
{
    return shelf->kind != SPOKEN_NONE &&
           (named_next(library, shelf, item) || extra_next(library, shelf, item));
}

bool spoken_shelf_present(const Library *library, SpokenKind kind)
{
    uint16_t folder = 0;
    SpokenShelf shelf = {.kind = kind};
    return spoken_shelf_folder(library, kind, &folder) || extra_next(library, &shelf, NULL);
}

CORE_SEAM uint32_t spoken_shelf_count(const Library *library, SpokenShelf *shelf)
{
    SpokenShelf scan = {.kind = shelf->kind, .between = shelf->between, .context = shelf->context};
    uint32_t count = 0;
    while (next(library, &scan, NULL)) {
        count++;
    }
    return count;
}

static void seek_named(const Library *library, SpokenShelf *shelf, uint32_t position)
{
    LibraryFolder root;
    if (shelf->track != 0 || !library_read_folder(library, 0, &root)) {
        return;
    }
    while (shelf->root < root.subfolder_count) {
        uint16_t index;
        BrowseQuery query = browse_root_query(BROWSE_FOLDER);
        if (library_read_index(library, LIBRARY_LINKS, root.first_link + shelf->root, &index) &&
            root_matches(library, shelf, index)) {
            query.subject = index;
            uint32_t remaining = browse_count(library, query) - shelf->child;
            uint32_t skip = position - shelf->position;
            if (skip < remaining) {
                shelf->child += skip;
                shelf->position = position;
                return;
            }
            shelf->position += remaining;
        }
        service(shelf);
        shelf->root++;
        shelf->child = 0;
    }
}

CORE_SEAM bool spoken_shelf_item(const Library *library, SpokenShelf *shelf, uint32_t position,
                                 BrowseItem *item)
{
    if (shelf->cached && shelf->last_position == position) {
        *item = shelf->last;
        return true;
    }
    if (position < shelf->position) {
        spoken_shelf_init(shelf, shelf->kind, shelf->between, shelf->context);
    }
    seek_named(library, shelf, position);
    while (shelf->position <= position) {
        bool selected = shelf->position == position;
        if (!next(library, shelf, selected ? item : NULL)) {
            return false;
        }
        shelf->position++;
    }
    shelf->last = *item;
    shelf->last_position = position;
    shelf->cached = true;
    return true;
}
