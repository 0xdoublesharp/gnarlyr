#include "browse_sections.h"

#include "core/seam.h"
#include "library/sort_key.h"

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static uint16_t track_sort(const Library *library, LibraryFolder folder, int position)
{
    uint16_t id;
    LibraryTrack track;
    return library_read_index(library, LIBRARY_LINKS,
                              folder.first_link + folder.subfolder_count + (uint32_t)position,
                              &id) &&
                   library_read_track(library, id, &track)
               ? (uint16_t)((uint16_t)track.disc * 256u + track.number)
               : 0;
}

static int track_bound(const Library *library, LibraryFolder folder, uint32_t key)
{
    int low = 0;
    int high = folder.track_count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (track_sort(library, folder, middle) < key) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

static ListSections folder_range(const Library *library, uint16_t subject, int first, int index)
{
    LibraryFolder folder;
    if (!library_read_folder(library, subject, &folder)) {
        return (ListSections){0, 0};
    }
    int tracks = first + folder.subfolder_count;
    if (index < tracks) {
        return (ListSections){first, tracks};
    }
    if ((folder.flags & LIBRARY_FOLDER_SHEET) != 0 || index >= tracks + folder.track_count) {
        return (ListSections){0, 0};
    }
    uint32_t key = track_sort(library, folder, index - tracks);
    ListSections range = {tracks + track_bound(library, folder, key),
                          tracks + track_bound(library, folder, key + 1)};
    return range.end - range.first > 1 ? range : (ListSections){0, 0};
}

static ListSections shelf_range(const BrowseScreen *browse, int position)
{
    const Library *library = browse->pool->config.library;
    LibraryFolder root;
    if (!library_read_folder(library, 0, &root)) {
        return (ListSections){0, 0};
    }
    int first = 0;
    for (uint32_t link = 0; link < root.subfolder_count; link++) {
        uint16_t id;
        LibraryFolder folder;
        char name[BROWSE_LABEL_CAPACITY];
        if (!library_read_index(library, LIBRARY_LINKS, root.first_link + link, &id) ||
            !library_read_folder(library, id, &folder) ||
            !library_read_string(library, folder.name, name, sizeof name)) {
            return (ListSections){0, 0};
        }
        if (spoken_kind_of_name(name, text_length(name)) != browse->shelf) {
            continue;
        }
        int end = first + folder.subfolder_count + folder.track_count;
        if (position < end) {
            return folder_range(library, id, first, position);
        }
        first = end;
    }
    return (ListSections){0, 0};
}

ListSections browse_sections_range(const BrowseScreen *browse, int index)
{
    const Library *library = browse->pool->config.library;
    if (browse->shelf_root) {
        return shelf_range(browse, index);
    }
    if (browse->favorites_only) {
        return (ListSections){0, (int)browse->count};
    }
    switch (browse->query.kind) {
    case BROWSE_ARTISTS:
    case BROWSE_ALBUM_ARTISTS:
    case BROWSE_ALBUMS:
    case BROWSE_ALL_SONGS:
    case BROWSE_GENRES:
        return (ListSections){0, (int)browse->count};
    case BROWSE_PLAYLISTS: {
        int stored = (int)library->sections[LIBRARY_PLAYLISTS].count;
        return index < stored ? (ListSections){0, stored}
                              : (ListSections){stored, (int)browse->count};
    }
    case BROWSE_GROUP_ALBUMS:
        return (ListSections){0, browse->query.group == LIBRARY_GENRES ? (int)browse->count : 0};
    case BROWSE_FOLDER:
        return folder_range(library, browse->query.subject, 0, index);
    default:
        return (ListSections){0, 0};
    }
}

CORE_SEAM int browse_sections_at(const BrowseScreen *browse, int index)
{
    const Library *library = browse->pool->config.library;
    BrowseItem item;
    char label[BROWSE_LABEL_CAPACITY];
    char detail[BROWSE_DETAIL_CAPACITY];
    uint32_t position =
        browse->favorites_only ? browse->pool->favorite_albums[index] : (uint32_t)index;
    bool found = browse->shelf_root
                     ? spoken_shelf_item(library, &browse->pool->shelf_cursor, position, &item)
                     : browse_item(library, browse->query, position, &item);
    if (!found || !browse_item_text(library, browse->query, item,
                                    (BrowseText){label, sizeof label, detail, sizeof detail})) {
        return LIST_SECTION_BEFORE_LETTERS;
    }
    char key[SORT_KEY_CAPACITY];
    sort_key_make(label, key, sizeof key);
    return list_section_of_key(key);
}
