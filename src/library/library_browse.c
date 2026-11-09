#include "library/library_browse.h"

#include "core/format.h"
#include "core/string_builder.h"
#include "core/utf8.h"

enum {
    PATH_CAPACITY = 256,
    ROOT_FOLDER = 0,
    TREE_DEPTH = 64
};

static BrowseItem make_item(BrowseItemKind kind, LibrarySection group, uint32_t index)
{
    return (BrowseItem){.kind = kind, .group = group, .index = (uint16_t)index};
}

static LibrarySection group_section_of(BrowseKind kind)
{
    switch (kind) {
    case BROWSE_ALBUM_ARTISTS:
        return LIBRARY_ALBUM_ARTISTS;
    case BROWSE_GENRES:
        return LIBRARY_GENRES;
    case BROWSE_PLAYLISTS:
    case BROWSE_PLAYLIST_TRACKS:
        return LIBRARY_PLAYLISTS;
    case BROWSE_ARTISTS:
    case BROWSE_ALBUMS:
    case BROWSE_ALL_SONGS:
    case BROWSE_GROUP_ALBUMS:
    case BROWSE_ALBUM_TRACKS:
    case BROWSE_FOLDER:
    case BROWSE_FOLDER_TREE:
        return LIBRARY_ARTISTS;
    }
    return LIBRARY_ARTISTS;
}

BrowseQuery browse_root_query(BrowseKind kind)
{
    return (BrowseQuery){
        .kind = kind, .group = group_section_of(kind), .subject = 0, .artist = LIBRARY_NONE};
}

static bool track_matches(const Library *library, BrowseQuery query, uint32_t track_index)
{
    LibraryTrack track;
    if (query.artist == LIBRARY_NONE) {
        return true;
    }
    return library_read_track(library, track_index, &track) && track.artist == query.artist;
}

static uint32_t group_link_count(const Library *library, BrowseQuery query)
{
    LibraryGroup group;
    return library_read_group(library, query.group, query.subject, &group) ? group.link_count : 0;
}

static uint32_t album_track_count(const Library *library, BrowseQuery query)
{
    LibraryAlbum album;
    if (!library_read_album(library, query.subject, &album)) {
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t track = album.first_track; track < album.first_track + album.track_count;
         track++) {
        count += track_matches(library, query, track) ? 1 : 0;
    }
    return count;
}

static bool is_sheet(const Library *library, uint32_t index)
{
    LibraryFolder folder;
    return library_read_folder(library, index, &folder) &&
           (folder.flags & LIBRARY_FOLDER_SHEET) != 0;
}

static bool track_of_folder(const Library *library, uint16_t folder, bool last, uint16_t *track)
{
    LibraryFolder record;
    if (!library_read_folder(library, folder, &record) || record.track_count == 0) {
        return false;
    }
    uint32_t position = last ? (uint32_t)record.track_count - 1u : 0;
    return library_read_index(library, LIBRARY_LINKS,
                              record.first_link + record.subfolder_count + position, track);
}

static bool sheet_of(const Library *library, uint16_t folder, uint16_t track, uint16_t *sheet)
{
    LibraryFolder record;
    if (!library_read_folder(library, folder, &record)) {
        return false;
    }
    for (uint32_t index = 0; index < record.subfolder_count; index++) {
        uint16_t child = 0;
        uint16_t first = 0;
        if (library_read_index(library, LIBRARY_LINKS, record.first_link + index, &child) &&
            is_sheet(library, child) && track_of_folder(library, child, false, &first) &&
            first == track) {
            *sheet = child;
            return true;
        }
    }
    return false;
}

static uint32_t tree_count(const Library *library, uint16_t root)
{
    LibraryFolder folder;
    uint32_t total = 0;
    for (uint32_t index = root;
         library_read_folder(library, index, &folder) && (index == root || folder.parent >= root);
         index++) {
        total += folder.track_count;
        total -= index != root && (folder.flags & 3) == LIBRARY_FOLDER_SHEET ? 1 : 0;
    }
    return total;
}

static bool tree_descend(const Library *library, uint16_t *current, uint32_t *position)
{
    LibraryFolder folder;
    if (!library_read_folder(library, *current, &folder)) {
        return false;
    }
    for (uint32_t index = 0; index < folder.subfolder_count; index++) {
        uint16_t child = 0;
        if (!library_read_index(library, LIBRARY_LINKS, folder.first_link + index, &child)) {
            return false;
        }
        uint32_t count = tree_count(library, child);
        if (*position < count) {
            *current = child;
            return true;
        }
        *position -= count;
    }
    return false;
}

static bool tree_own_track(const Library *library, uint16_t index, uint32_t position,
                           uint16_t *track)
{
    LibraryFolder folder;
    uint16_t sheet = 0;
    if (!library_read_folder(library, index, &folder)) {
        return false;
    }
    for (uint32_t link = 0; link < folder.track_count; link++) {
        if (!library_read_index(library, LIBRARY_LINKS,
                                folder.first_link + folder.subfolder_count + link, track)) {
            return false;
        }
        if (!sheet_of(library, index, *track, &sheet) && position-- == 0) {
            return true;
        }
    }
    return false;
}

static bool tree_item(const Library *library, uint16_t subject, uint32_t position, BrowseItem *item)
{
    uint16_t current = subject;
    uint16_t track = 0;
    for (int depth = 0; depth < TREE_DEPTH && tree_descend(library, &current, &position); depth++) {
    }
    if (!tree_own_track(library, current, position, &track)) {
        return false;
    }
    *item = make_item(BROWSE_ITEM_TRACK, LIBRARY_ARTISTS, track);
    return true;
}

static uint32_t folder_item_count(const Library *library, uint32_t subject)
{
    LibraryFolder folder;
    if (!library_read_folder(library, subject, &folder)) {
        return 0;
    }
    return (uint32_t)folder.subfolder_count + folder.track_count;
}

uint32_t browse_count(const Library *library, BrowseQuery query)
{
    switch (query.kind) {
    case BROWSE_ARTISTS:
        return library_music_count(library, LIBRARY_ARTISTS);
    case BROWSE_ALBUM_ARTISTS:
        return library_music_count(library, LIBRARY_ALBUM_ARTISTS);
    case BROWSE_GENRES:
        return library_music_count(library, LIBRARY_GENRES);
    case BROWSE_ALBUMS:
        return library_music_count(library, LIBRARY_ALBUMS);
    case BROWSE_ALL_SONGS:
        return library_music_count(library, LIBRARY_TITLE_ORDER);
    case BROWSE_GROUP_ALBUMS:
    case BROWSE_PLAYLIST_TRACKS:
        return group_link_count(library, query);
    case BROWSE_ALBUM_TRACKS:
        return album_track_count(library, query);
    case BROWSE_FOLDER:
        return folder_item_count(library, query.subject);
    case BROWSE_FOLDER_TREE:
        return tree_count(library, query.subject);
    case BROWSE_PLAYLISTS:
        return library_count(library, LIBRARY_PLAYLISTS);
    }
    return 0;
}

static bool table_item(const Library *library, LibrarySection section, uint32_t position,
                       BrowseItem *item)
{
    if (position >= library_music_count(library, section)) {
        return false;
    }
    BrowseItemKind kind = section == LIBRARY_ALBUMS ? BROWSE_ITEM_ALBUM : BROWSE_ITEM_GROUP;
    *item = make_item(kind, section, position);
    return true;
}

static bool linked_item(const Library *library, uint32_t link, BrowseItemKind kind,
                        BrowseItem *item)
{
    uint16_t value = 0;
    if (!library_read_index(library, LIBRARY_LINKS, link, &value)) {
        return false;
    }
    *item = make_item(kind, LIBRARY_ARTISTS, value);
    return true;
}

static bool title_order_item(const Library *library, uint32_t position, BrowseItem *item)
{
    uint16_t track = 0;
    if (!library_read_index(library, LIBRARY_TITLE_ORDER, position, &track)) {
        return false;
    }
    *item = make_item(BROWSE_ITEM_TRACK, LIBRARY_ARTISTS, track);
    return true;
}

static bool group_link_item(const Library *library, BrowseQuery query, uint32_t position,
                            BrowseItem *item)
{
    LibraryGroup group;
    if (!library_read_group(library, query.group, query.subject, &group) ||
        position >= group.link_count) {
        return false;
    }
    BrowseItemKind kind =
        query.kind == BROWSE_PLAYLIST_TRACKS ? BROWSE_ITEM_TRACK : BROWSE_ITEM_ALBUM;
    return linked_item(library, group.first_link + position, kind, item);
}

static bool album_track_item(const Library *library, BrowseQuery query, uint32_t position,
                             BrowseItem *item)
{
    LibraryAlbum album;
    if (!library_read_album(library, query.subject, &album)) {
        return false;
    }
    uint32_t seen = 0;
    for (uint32_t track = album.first_track; track < album.first_track + album.track_count;
         track++) {
        if (track_matches(library, query, track) && seen++ == position) {
            *item = make_item(BROWSE_ITEM_TRACK, LIBRARY_ARTISTS, track);
            return true;
        }
    }
    return false;
}

static bool folder_item(const Library *library, uint32_t subject, uint32_t position,
                        BrowseItem *item)
{
    LibraryFolder folder;
    if (!library_read_folder(library, subject, &folder) ||
        position >= (uint32_t)folder.subfolder_count + folder.track_count) {
        return false;
    }
    BrowseItemKind kind =
        position < folder.subfolder_count ? BROWSE_ITEM_FOLDER : BROWSE_ITEM_TRACK;
    return linked_item(library, folder.first_link + position, kind, item);
}

bool browse_item(const Library *library, BrowseQuery query, uint32_t position, BrowseItem *item)
{
    switch (query.kind) {
    case BROWSE_ARTISTS:
    case BROWSE_ALBUM_ARTISTS:
    case BROWSE_GENRES:
    case BROWSE_PLAYLISTS:
        return table_item(library, group_section_of(query.kind), position, item);
    case BROWSE_ALBUMS:
        return table_item(library, LIBRARY_ALBUMS, position, item);
    case BROWSE_ALL_SONGS:
        return title_order_item(library, position, item);
    case BROWSE_GROUP_ALBUMS:
    case BROWSE_PLAYLIST_TRACKS:
        return group_link_item(library, query, position, item);
    case BROWSE_ALBUM_TRACKS:
        return album_track_item(library, query, position, item);
    case BROWSE_FOLDER:
        return folder_item(library, query.subject, position, item);
    case BROWSE_FOLDER_TREE:
        return tree_item(library, query.subject, position, item);
    }
    return false;
}

static uint16_t artist_filter(BrowseQuery parent)
{
    bool from_artist = parent.kind == BROWSE_GROUP_ALBUMS && parent.group == LIBRARY_ARTISTS;
    return from_artist ? parent.subject : LIBRARY_NONE;
}

static BrowseQuery query_of(BrowseKind kind, LibrarySection group, uint16_t subject,
                            uint16_t artist)
{
    return (BrowseQuery){.kind = kind, .group = group, .subject = subject, .artist = artist};
}

bool browse_child(BrowseQuery parent, BrowseItem item, BrowseQuery *child)
{
    switch (item.kind) {
    case BROWSE_ITEM_GROUP:
        *child =
            query_of(item.group == LIBRARY_PLAYLISTS ? BROWSE_PLAYLIST_TRACKS : BROWSE_GROUP_ALBUMS,
                     item.group, item.index, LIBRARY_NONE);
        return true;
    case BROWSE_ITEM_ALBUM:
        *child = query_of(BROWSE_ALBUM_TRACKS, LIBRARY_ARTISTS, item.index, artist_filter(parent));
        return true;
    case BROWSE_ITEM_FOLDER:
        *child = query_of(BROWSE_FOLDER, LIBRARY_ARTISTS, item.index, LIBRARY_NONE);
        return true;
    case BROWSE_ITEM_TRACK:
        return false;
    }
    return false;
}

static void copy_span(char *target, size_t capacity, const char *source, size_t length)
{
    size_t kept = length < capacity ? length : capacity - 1;
    kept = utf8_complete_length(source, kept);
    for (size_t index = 0; index < kept; index++) {
        target[index] = source[index];
    }
    target[kept] = '\0';
}

static void copy_file_name(char *target, size_t capacity, const char *path)
{
    size_t start = 0;
    size_t length = 0;
    for (; path[length] != '\0'; length++) {
        start = path[length] == '/' ? length + 1 : start;
    }
    copy_span(target, capacity, path + start, length - start);
}

static bool group_text(const Library *library, BrowseItem item, BrowseText text)
{
    LibraryGroup group;
    return library_read_group(library, item.group, item.index, &group) &&
           library_read_string(library, group.name, text.label, text.label_capacity);
}

static bool album_text(const Library *library, BrowseItem item, BrowseText text)
{
    LibraryAlbum album;
    if (!library_read_album(library, item.index, &album)) {
        return false;
    }
    if (album.year > 0) {
        StringBuilder builder = string_builder_make(text.detail, text.detail_capacity);
        string_builder_append_unsigned(&builder, album.year);
    }
    return library_read_string(library, album.title, text.label, text.label_capacity);
}

static bool track_label(const Library *library, BrowseQuery query, LibraryTrack track,
                        BrowseText text)
{
    if (query.kind != BROWSE_FOLDER || is_sheet(library, query.subject)) {
        return library_read_string(library, track.title, text.label, text.label_capacity);
    }
    char path[PATH_CAPACITY];
    if (!library_read_string(library, track.path, path, sizeof path)) {
        return false;
    }
    copy_file_name(text.label, text.label_capacity, path);
    return true;
}

static uint32_t listed_length(const Library *library, BrowseQuery query, uint16_t index,
                              LibraryTrack track)
{
    uint16_t sheet = 0;
    uint16_t last = 0;
    LibraryTrack final;
    if (query.kind != BROWSE_FOLDER || (track.start_ms == 0 && track.end_ms == 0) ||
        !sheet_of(library, query.subject, index, &sheet) ||
        !track_of_folder(library, sheet, true, &last) ||
        !library_read_track(library, last, &final)) {
        return track.duration_ms;
    }
    return final.start_ms + final.duration_ms;
}

static bool track_text(const Library *library, BrowseQuery query, BrowseItem item, BrowseText text)
{
    LibraryTrack track;
    if (!library_read_track(library, item.index, &track)) {
        return false;
    }
    format_duration(listed_length(library, query, item.index, track), text.detail,
                    text.detail_capacity);
    return track_label(library, query, track, text);
}

static bool folder_text(const Library *library, BrowseItem item, BrowseText text)
{
    LibraryFolder folder;
    return library_read_folder(library, item.index, &folder) &&
           library_read_string(library, folder.name, text.label, text.label_capacity);
}

bool browse_item_text(const Library *library, BrowseQuery query, BrowseItem item, BrowseText text)
{
    text.label[0] = '\0';
    text.detail[0] = '\0';
    switch (item.kind) {
    case BROWSE_ITEM_GROUP:
        return group_text(library, item, text);
    case BROWSE_ITEM_ALBUM:
        return album_text(library, item, text);
    case BROWSE_ITEM_TRACK:
        return track_text(library, query, item, text);
    case BROWSE_ITEM_FOLDER:
        return folder_text(library, item, text);
    }
    return false;
}

static bool fixed_title(const char *title, char *buffer, size_t capacity)
{
    StringBuilder builder = string_builder_make(buffer, capacity);
    string_builder_append(&builder, title);
    return true;
}

static bool subject_title(const Library *library, BrowseQuery query, char *buffer, size_t capacity)
{
    LibraryGroup group;
    LibraryAlbum album;
    LibraryFolder folder;
    switch (query.kind) {
    case BROWSE_GROUP_ALBUMS:
    case BROWSE_PLAYLIST_TRACKS:
        return library_read_group(library, query.group, query.subject, &group) &&
               library_read_string(library, group.name, buffer, capacity);
    case BROWSE_ALBUM_TRACKS:
        return library_read_album(library, query.subject, &album) &&
               library_read_string(library, album.title, buffer, capacity);
    case BROWSE_FOLDER:
        return library_read_folder(library, query.subject, &folder) &&
               library_read_string(library, folder.name, buffer, capacity);
    case BROWSE_ARTISTS:
    case BROWSE_ALBUM_ARTISTS:
    case BROWSE_GENRES:
    case BROWSE_ALBUMS:
    case BROWSE_ALL_SONGS:
    case BROWSE_PLAYLISTS:
    case BROWSE_FOLDER_TREE:
        return false;
    }
    return false;
}

bool browse_title(const Library *library, BrowseQuery query, char *buffer, size_t capacity)
{
    switch (query.kind) {
    case BROWSE_ARTISTS:
        return fixed_title("Artists", buffer, capacity);
    case BROWSE_ALBUM_ARTISTS:
        return fixed_title("Album Artists", buffer, capacity);
    case BROWSE_GENRES:
        return fixed_title("Genres", buffer, capacity);
    case BROWSE_ALBUMS:
        return fixed_title("Albums", buffer, capacity);
    case BROWSE_ALL_SONGS:
        return fixed_title("All Songs", buffer, capacity);
    case BROWSE_PLAYLISTS:
        return fixed_title("Playlists", buffer, capacity);
    case BROWSE_FOLDER:
        if (query.subject == ROOT_FOLDER) {
            return fixed_title("Folders", buffer, capacity);
        }
        return subject_title(library, query, buffer, capacity);
    case BROWSE_GROUP_ALBUMS:
    case BROWSE_ALBUM_TRACKS:
    case BROWSE_PLAYLIST_TRACKS:
    case BROWSE_FOLDER_TREE:
        return subject_title(library, query, buffer, capacity);
    }
    return false;
}

BrowseQuery browse_play_source(const Library *library, BrowseQuery query, uint32_t *position)
{
    BrowseItem item;
    LibraryTrack track;
    uint16_t sheet = 0;
    if (query.kind != BROWSE_FOLDER || !browse_item(library, query, *position, &item) ||
        item.kind != BROWSE_ITEM_TRACK || !library_read_track(library, item.index, &track) ||
        (track.start_ms == 0 && track.end_ms == 0) ||
        !sheet_of(library, query.subject, item.index, &sheet)) {
        return query;
    }
    *position = 0;
    return query_of(BROWSE_FOLDER, LIBRARY_ARTISTS, sheet, LIBRARY_NONE);
}
