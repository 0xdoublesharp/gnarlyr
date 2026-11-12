#include "library/library_view.h"

#include <stddef.h>

#include "core/utf8.h"
#include "library/library_content.h"
#include "library/sort_key.h"

enum {
    BANK_BYTES = LIBRARY_VIEW_WORK_BYTES / 2,
    INPUT_TRACKS = 128,
    META_BYTES = 256,
    PATH_BYTES = 1025,
    FOLD_CHUNK_BYTES = 32,
    TREE_DEPTH = 64,
    TREE_BITMAP_BYTES = 8192,
    TREE_ZERO = 0,
    TREE_MARK,
    TREE_WALK
};

typedef struct FolderFrame {
    uint16_t folder;
    uint16_t next;
    uint8_t phase;
} FolderFrame;

typedef struct ViewPrivate {
    char needle[SORT_KEY_CAPACITY];
    uint16_t input[2][INPUT_TRACKS];
    uint16_t output[LIBRARY_VIEW_PAGE_TRACKS];
    uint32_t page[2];
    uint32_t source_position;
    uint32_t first;
    uint32_t width;
    uint32_t base;
    uint32_t left;
    uint32_t middle;
    uint32_t right;
    uint32_t end;
    uint32_t output_position;
    uint32_t output_first;
    uint16_t output_count;
    uint8_t bank;
    bool merging;
    uint8_t tree_phase;
    uint8_t depth;
    FolderFrame folders[TREE_DEPTH];
} ViewPrivate;

typedef enum NextTrack {
    NEXT_TRACK,
    NEXT_AGAIN,
    NEXT_END,
    NEXT_FAILED
} NextTrack;

_Static_assert(sizeof(ViewPrivate) <= LIBRARY_VIEW_PRIVATE_BYTES, "view state fits its storage");
_Static_assert(sizeof(LibraryView) <= 4096, "view state stays bounded");

static ViewPrivate *details(LibraryView *view)
{
    return (ViewPrivate *)(void *)view->private.bytes;
}

static const ViewPrivate *const_details(const LibraryView *view)
{
    return (const ViewPrivate *)(const void *)view->private.bytes;
}

static bool fail(LibraryView *view, LibraryViewState state)
{
    view->state = state;
    return false;
}

static bool work_read(LibraryView *view, uint32_t offset, void *bytes, uint32_t length)
{
    return view->work.read(view->work.context, offset, bytes, length) ||
           fail(view, LIBRARY_VIEW_READ_FAILED);
}

static bool work_write(LibraryView *view, uint32_t offset, const void *bytes, uint32_t length)
{
    return view->work.write(view->work.context, offset, bytes, length) ||
           fail(view, LIBRARY_VIEW_WRITE_FAILED);
}

static bool current(const LibraryView *view)
{
    uint32_t fingerprint;
    return library_content_fingerprint(view->library, &fingerprint) &&
           fingerprint == view->fingerprint;
}

LibraryViewConfig library_view_defaults(void)
{
    return (LibraryViewConfig){.source = browse_root_query(BROWSE_ALL_SONGS),
                               .fields = LIBRARY_VIEW_FIELDS_ALL,
                               .artist = LIBRARY_NONE,
                               .genre = LIBRARY_NONE,
                               .album = LIBRARY_NONE};
}

static void folded_text(const char *text, char *key)
{
    char prefixed[META_BYTES + 1];
    prefixed[0] = '~';
    size_t length = 0;
    while (length < META_BYTES - 1 && text[length] != '\0') {
        prefixed[length + 1] = text[length];
        length++;
    }
    prefixed[length + 1] = '\0';
    sort_key_make(prefixed, key, SORT_KEY_CAPACITY);
    for (size_t index = 0;; index++) {
        key[index] = key[index + 1];
        if (key[index] == '\0') {
            return;
        }
    }
}

static bool has_text(const char *text, const char *needle)
{
    for (size_t first = 0; text[first] != '\0'; first++) {
        size_t length = 0;
        while (needle[length] != '\0' && text[first + length] == needle[length]) {
            length++;
        }
        if (needle[length] == '\0') {
            return true;
        }
    }
    return needle[0] == '\0';
}

static bool valid_config(const LibraryViewConfig *config)
{
    for (size_t index = 0; index < LIBRARY_VIEW_TEXT_BYTES; index++) {
        if (config->text[index] == '\0') {
            return (uint32_t)config->sort <= LIBRARY_VIEW_SORT_DURATION &&
                   (config->fields & ~(uint32_t)LIBRARY_VIEW_FIELDS_ALL) == 0 &&
                   (config->year_max == 0 || config->year_min <= config->year_max) &&
                   (config->duration_max_ms == 0 ||
                    config->duration_min_ms <= config->duration_max_ms);
        }
    }
    return false;
}

static bool source_begin(LibraryView *view)
{
    ViewPrivate *job = details(view);
    BrowseQuery source = view->config.source;
    LibraryAlbum album;
    LibraryGroup playlist;
    switch (source.kind) {
    case BROWSE_ALL_SONGS:
        view->total = library_music_count(view->library, LIBRARY_TITLE_ORDER);
        return true;
    case BROWSE_ALBUM_TRACKS:
        if (!library_read_album(view->library, source.subject, &album)) {
            return fail(view, LIBRARY_VIEW_READ_FAILED);
        }
        if ((uint32_t)album.first_track + album.track_count >
            library_count(view->library, LIBRARY_TRACKS)) {
            return fail(view, LIBRARY_VIEW_READ_FAILED);
        }
        job->first = album.first_track;
        view->total = album.track_count;
        return true;
    case BROWSE_PLAYLIST_TRACKS:
        if (!library_read_group(view->library, LIBRARY_PLAYLISTS, source.subject, &playlist)) {
            return fail(view, LIBRARY_VIEW_READ_FAILED);
        }
        if (playlist.first_link > UINT32_MAX - playlist.link_count) {
            return fail(view, LIBRARY_VIEW_READ_FAILED);
        }
        job->first = playlist.first_link;
        view->total = playlist.link_count;
        return true;
    case BROWSE_FOLDER_TREE:
        if (source.subject >= library_count(view->library, LIBRARY_FOLDERS)) {
            return fail(view, LIBRARY_VIEW_INVALID);
        }
        view->total = library_count(view->library, LIBRARY_TRACKS);
        job->tree_phase = TREE_ZERO;
        return true;
    default:
        return fail(view, LIBRARY_VIEW_INVALID);
    }
}

LibraryViewState library_view_begin(LibraryView *view, const Library *library,
                                    const LibraryViewConfig *config, BuilderScratch work)
{
    *view = (LibraryView){
        .library = library, .config = *config, .work = work, .state = LIBRARY_VIEW_SCANNING};
    if (!valid_config(config) || work.read == NULL || work.write == NULL ||
        work.size < LIBRARY_VIEW_WORK_BYTES ||
        !library_content_fingerprint(library, &view->fingerprint)) {
        view->state = LIBRARY_VIEW_INVALID;
    } else {
        folded_text(config->text, details(view)->needle);
        source_begin(view);
    }
    return view->state;
}

static bool tree_bit(LibraryView *view, uint16_t track, bool set, bool *marked)
{
    if (track >= library_count(view->library, LIBRARY_TRACKS)) {
        return fail(view, LIBRARY_VIEW_READ_FAILED);
    }
    uint8_t byte;
    uint32_t offset = BANK_BYTES + track / 8u;
    uint8_t bit = (uint8_t)(1u << (track % 8u));
    if (!work_read(view, offset, &byte, 1)) {
        return false;
    }
    *marked = (byte & bit) != 0;
    byte |= bit;
    return !set || work_write(view, offset, &byte, 1);
}

static bool read_folder(LibraryView *view, uint32_t index, LibraryFolder *folder)
{
    return (library_read_folder(view->library, index, folder) &&
            (uint64_t)folder->first_link + folder->subfolder_count + folder->track_count <=
                library_count(view->library, LIBRARY_LINKS)) ||
           fail(view, LIBRARY_VIEW_READ_FAILED);
}

static NextTrack tree_prepare(LibraryView *view)
{
    ViewPrivate *job = details(view);
    if (job->tree_phase == TREE_ZERO) {
        uint8_t zeros[512] = {0};
        if (!work_write(view, BANK_BYTES + job->source_position, zeros, sizeof zeros)) {
            return NEXT_FAILED;
        }
        job->source_position += sizeof zeros;
        if (job->source_position == TREE_BITMAP_BYTES) {
            job->tree_phase = TREE_MARK;
            job->source_position = 0;
        }
        return NEXT_AGAIN;
    }
    if (job->source_position >= library_count(view->library, LIBRARY_FOLDERS)) {
        job->tree_phase = TREE_WALK;
        job->depth = 1;
        job->folders[0] = (FolderFrame){.folder = view->config.source.subject};
        return NEXT_AGAIN;
    }
    LibraryFolder folder;
    if (!read_folder(view, job->source_position++, &folder)) {
        return NEXT_FAILED;
    }
    uint16_t track;
    bool marked;
    if ((folder.flags & LIBRARY_FOLDER_SHEET) != 0 && folder.track_count != 0 &&
        (!library_read_index(view->library, LIBRARY_LINKS,
                             folder.first_link + folder.subfolder_count, &track) ||
         !tree_bit(view, track, true, &marked))) {
        return NEXT_FAILED;
    }
    return NEXT_AGAIN;
}

static NextTrack tree_next(LibraryView *view, uint16_t *track)
{
    ViewPrivate *job = details(view);
    if (job->tree_phase != TREE_WALK) {
        return tree_prepare(view);
    }
    if (job->depth == 0) {
        return NEXT_END;
    }
    FolderFrame *frame = &job->folders[job->depth - 1];
    LibraryFolder folder;
    if (!read_folder(view, frame->folder, &folder)) {
        return NEXT_FAILED;
    }
    if (frame->phase == 0 && frame->next < folder.subfolder_count) {
        uint16_t child;
        if (!library_read_index(view->library, LIBRARY_LINKS, folder.first_link + frame->next++,
                                &child) ||
            job->depth == TREE_DEPTH) {
            return NEXT_FAILED;
        }
        for (uint8_t depth = 0; depth < job->depth; depth++) {
            if (job->folders[depth].folder == child) {
                return NEXT_FAILED;
            }
        }
        job->folders[job->depth++] = (FolderFrame){.folder = child};
        return NEXT_AGAIN;
    }
    if (frame->phase == 0) {
        frame->phase = 1;
        frame->next = 0;
        return NEXT_AGAIN;
    }
    if (frame->next >= folder.track_count) {
        job->depth--;
        return NEXT_AGAIN;
    }
    if (!library_read_index(view->library, LIBRARY_LINKS,
                            folder.first_link + folder.subfolder_count + frame->next++, track)) {
        return NEXT_FAILED;
    }
    bool marked = false;
    if ((folder.flags & LIBRARY_FOLDER_SHEET) == 0 && !tree_bit(view, *track, false, &marked)) {
        return NEXT_FAILED;
    }
    return marked ? NEXT_AGAIN : NEXT_TRACK;
}

static NextTrack next_track(LibraryView *view, uint16_t *track)
{
    ViewPrivate *job = details(view);
    if (view->config.source.kind == BROWSE_FOLDER_TREE) {
        return tree_next(view, track);
    }
    if (job->source_position >= view->total) {
        return NEXT_END;
    }
    uint32_t position = job->source_position++;
    if (view->config.source.kind == BROWSE_ALBUM_TRACKS) {
        *track = (uint16_t)(job->first + position);
        return NEXT_TRACK;
    }
    LibrarySection section =
        view->config.source.kind == BROWSE_ALL_SONGS ? LIBRARY_TITLE_ORDER : LIBRARY_LINKS;
    return library_read_index(view->library, section, job->first + position, track) ? NEXT_TRACK
                                                                                    : NEXT_FAILED;
}

static bool album_of(LibraryView *view, const LibraryTrack *track, LibraryAlbum *album)
{
    return library_read_album(view->library, track->album, album) ||
           fail(view, LIBRARY_VIEW_READ_FAILED);
}

static bool filters_match(LibraryView *view, uint16_t id, const LibraryTrack *track)
{
    const LibraryViewConfig *config = &view->config;
    if ((config->artist != LIBRARY_NONE && config->artist != track->artist) ||
        (config->genre != LIBRARY_NONE && config->genre != track->genre) ||
        (config->album != LIBRARY_NONE && config->album != track->album) ||
        (config->source.artist != LIBRARY_NONE && config->source.artist != track->artist) ||
        track->duration_ms < config->duration_min_ms ||
        (config->duration_max_ms != 0 && track->duration_ms > config->duration_max_ms)) {
        return false;
    }
    if (config->year_min != 0 || config->year_max != 0) {
        LibraryAlbum album;
        if (!album_of(view, track, &album) || album.year < config->year_min ||
            (config->year_max != 0 && album.year > config->year_max)) {
            return false;
        }
    }
    (void)id;
    return true;
}

static bool predicate_matches(LibraryView *view, uint16_t id, const LibraryTrack *track)
{
    const LibraryViewConfig *config = &view->config;
    LibraryViewAcceptance accepted =
        config->accept == NULL ? LIBRARY_VIEW_ACCEPT : config->accept(config->context, id, track);
    if (accepted == LIBRARY_VIEW_ACCEPT_FAILED) {
        return fail(view, LIBRARY_VIEW_READ_FAILED);
    }
    return accepted == LIBRARY_VIEW_ACCEPT;
}

static bool group_name(LibraryView *view, LibrarySection section, uint16_t index, uint32_t *offset)
{
    LibraryGroup group;
    if (!library_read_group(view->library, section, index, &group)) {
        return fail(view, LIBRARY_VIEW_READ_FAILED);
    }
    *offset = group.name;
    return true;
}

static bool field_offset(LibraryView *view, const LibraryTrack *track, uint32_t field,
                         uint32_t *offset)
{
    LibraryAlbum album;
    switch (field) {
    case LIBRARY_VIEW_FIELD_TITLE:
        *offset = track->title;
        return true;
    case LIBRARY_VIEW_FIELD_PATH:
        *offset = track->path;
        return true;
    case LIBRARY_VIEW_FIELD_ARTIST:
        return group_name(view, LIBRARY_ARTISTS, track->artist, offset);
    case LIBRARY_VIEW_FIELD_GENRE:
        return group_name(view, LIBRARY_GENRES, track->genre, offset);
    case LIBRARY_VIEW_FIELD_ALBUM:
    case LIBRARY_VIEW_FIELD_ALBUM_ARTIST:
        if (!album_of(view, track, &album)) {
            return false;
        }
        if (field == LIBRARY_VIEW_FIELD_ALBUM) {
            *offset = album.title;
            return true;
        }
        return group_name(view, LIBRARY_ALBUM_ARTISTS, album.album_artist, offset);
    default:
        return fail(view, LIBRARY_VIEW_INVALID);
    }
}

static bool field_text(LibraryView *view, const LibraryTrack *track, uint32_t field, char *text)
{
    uint32_t offset;
    return (field_offset(view, track, field, &offset) &&
            library_read_string(view->library, offset, text, META_BYTES)) ||
           fail(view, LIBRARY_VIEW_READ_FAILED);
}

static bool field_key(LibraryView *view, const LibraryTrack *track, uint32_t field, char *key)
{
    char text[META_BYTES];
    if (!field_text(view, track, field, text)) {
        return false;
    }
    sort_key_make(text, key, SORT_KEY_CAPACITY);
    return true;
}

typedef struct PathMatcher {
    const char *needle;
    uint16_t prefix[SORT_KEY_CAPACITY];
    uint16_t matched;
    uint16_t length;
} PathMatcher;

static void matcher_begin(PathMatcher *matcher, const char *needle)
{
    *matcher = (PathMatcher){.needle = needle};
    while (needle[matcher->length] != '\0') {
        matcher->length++;
    }
    for (uint16_t index = 1, matched = 0; index < matcher->length; index++) {
        while (matched != 0 && needle[index] != needle[matched]) {
            matched = matcher->prefix[matched - 1];
        }
        if (needle[index] == needle[matched]) {
            matched++;
        }
        matcher->prefix[index] = matched;
    }
}

static bool matcher_append(PathMatcher *matcher, const char *text, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        while (matcher->matched != 0 && text[index] != matcher->needle[matcher->matched]) {
            matcher->matched = matcher->prefix[matcher->matched - 1];
        }
        if (text[index] == matcher->needle[matcher->matched]) {
            matcher->matched++;
        }
        if (matcher->matched == matcher->length) {
            return true;
        }
    }
    return false;
}

static bool path_text_matches(LibraryView *view, const LibraryTrack *track, const char *needle)
{
    char text[PATH_BYTES + 4] = {0};
    if (!library_read_string(view->library, track->path, text, PATH_BYTES + 1)) {
        return fail(view, LIBRARY_VIEW_READ_FAILED);
    }
    size_t length = 0;
    while (text[length] != '\0' && length < PATH_BYTES) {
        length++;
    }
    if (length >= PATH_BYTES) {
        return fail(view, LIBRARY_VIEW_READ_FAILED);
    }
    PathMatcher matcher;
    matcher_begin(&matcher, needle);
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *first = cursor;
        while (*cursor != '\0' && (size_t)(cursor - first) < FOLD_CHUNK_BYTES) {
            utf8_next(&cursor);
        }
        char chunk[FOLD_CHUNK_BYTES + 6];
        size_t count = (size_t)(cursor - first);
        chunk[0] = '~';
        for (size_t index = 0; index < count; index++) {
            chunk[index + 1] = first[index];
        }
        chunk[count + 1] = '~';
        chunk[count + 2] = '\0';
        char folded[SORT_KEY_CAPACITY];
        size_t used = sort_key_make(chunk, folded, sizeof folded);
        if (used < 2 || folded[used - 1] != '~') {
            return fail(view, LIBRARY_VIEW_READ_FAILED);
        }
        if (matcher_append(&matcher, folded + 1, used - 2)) {
            return true;
        }
    }
    return false;
}

static bool text_matches(LibraryView *view, const LibraryTrack *track)
{
    const char *needle = details(view)->needle;
    if (*needle == '\0') {
        return true;
    }
    char key[SORT_KEY_CAPACITY];
    for (uint32_t field = 1; field <= LIBRARY_VIEW_FIELD_PATH; field <<= 1) {
        if ((view->config.fields & field) != 0) {
            if (field == LIBRARY_VIEW_FIELD_PATH) {
                return path_text_matches(view, track, needle);
            }
            char text[META_BYTES];
            if (!field_text(view, track, field, text)) {
                return false;
            }
            folded_text(text, key);
            if (has_text(key, needle)) {
                return true;
            }
        }
    }
    return false;
}

static bool flush_output(LibraryView *view, uint8_t bank)
{
    ViewPrivate *job = details(view);
    if (job->output_count == 0) {
        return true;
    }
    if (!work_write(view, bank * BANK_BYTES + job->output_first * 2, job->output,
                    job->output_count * 2u)) {
        return false;
    }
    job->output_count = 0;
    return true;
}

static bool append_track(LibraryView *view, uint16_t track, uint32_t position, uint8_t bank)
{
    ViewPrivate *job = details(view);
    if (job->output_count == 0) {
        job->output_first = position;
    }
    job->output[job->output_count++] = track;
    return job->output_count < LIBRARY_VIEW_PAGE_TRACKS || flush_output(view, bank);
}

static void finish_scan(LibraryView *view)
{
    ViewPrivate *job = details(view);
    if (!flush_output(view, 0)) {
        return;
    }
    job->bank = 0;
    if (view->count < 2 || view->config.sort == LIBRARY_VIEW_SORT_ORIGINAL ||
        (view->config.sort == LIBRARY_VIEW_SORT_TITLE && !view->config.descending &&
         view->config.source.kind == BROWSE_ALL_SONGS)) {
        view->state = LIBRARY_VIEW_READY;
        return;
    }
    job->width = 1;
    job->page[0] = job->page[1] = UINT32_MAX;
    view->state = LIBRARY_VIEW_SORTING;
}

static void scan_step(LibraryView *view)
{
    uint16_t track;
    NextTrack next = next_track(view, &track);
    if (next == NEXT_END) {
        finish_scan(view);
    } else if (next == NEXT_FAILED) {
        if (view->state == LIBRARY_VIEW_SCANNING) {
            fail(view, LIBRARY_VIEW_READ_FAILED);
        }
    } else if (next == NEXT_TRACK) {
        view->scanned++;
        LibraryTrack record;
        if (!library_read_track(view->library, track, &record)) {
            fail(view, LIBRARY_VIEW_READ_FAILED);
        } else if (filters_match(view, track, &record) && text_matches(view, &record) &&
                   predicate_matches(view, track, &record)) {
            if (view->count == UINT16_MAX) {
                fail(view, LIBRARY_VIEW_INVALID);
            } else if (append_track(view, track, view->count, 0)) {
                view->count++;
            }
        }
    }
}

static bool input_track(LibraryView *view, uint32_t position, int slot, uint16_t *track)
{
    ViewPrivate *job = details(view);
    uint32_t page = position / INPUT_TRACKS;
    if (job->page[slot] != page) {
        uint32_t first = page * INPUT_TRACKS;
        uint32_t count = view->count - first;
        count = count < INPUT_TRACKS ? count : INPUT_TRACKS;
        if (!work_read(view, job->bank * BANK_BYTES + first * 2, job->input[slot], count * 2)) {
            return false;
        }
        job->page[slot] = page;
    }
    *track = job->input[slot][position % INPUT_TRACKS];
    return true;
}

static int compare_number(uint32_t left, uint32_t right)
{
    return left < right ? -1 : left > right ? 1 : 0;
}

static int compare_keys(const char *left, const char *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static int compare_tracks(LibraryView *view, uint16_t left, uint16_t right)
{
    LibraryTrack a;
    LibraryTrack b;
    if (!library_read_track(view->library, left, &a) ||
        !library_read_track(view->library, right, &b)) {
        fail(view, LIBRARY_VIEW_READ_FAILED);
        return 0;
    }
    if (view->config.sort == LIBRARY_VIEW_SORT_DURATION) {
        return compare_number(a.duration_ms, b.duration_ms);
    }
    if (view->config.sort == LIBRARY_VIEW_SORT_YEAR) {
        LibraryAlbum a_album;
        LibraryAlbum b_album;
        return album_of(view, &a, &a_album) && album_of(view, &b, &b_album)
                   ? compare_number(a_album.year, b_album.year)
                   : 0;
    }
    uint32_t field = view->config.sort == LIBRARY_VIEW_SORT_TITLE   ? LIBRARY_VIEW_FIELD_TITLE
                     : view->config.sort == LIBRARY_VIEW_SORT_ALBUM ? LIBRARY_VIEW_FIELD_ALBUM
                                                                    : LIBRARY_VIEW_FIELD_ARTIST;
    char a_key[SORT_KEY_CAPACITY];
    char b_key[SORT_KEY_CAPACITY];
    return field_key(view, &a, field, a_key) && field_key(view, &b, field, b_key)
               ? compare_keys(a_key, b_key)
               : 0;
}

static void merge_begin(LibraryView *view)
{
    ViewPrivate *job = details(view);
    job->left = job->base;
    job->middle = job->base + job->width;
    if (job->middle > view->count) {
        job->middle = view->count;
    }
    job->right = job->middle;
    job->end = job->middle + job->width;
    if (job->end > view->count) {
        job->end = view->count;
    }
    job->output_position = job->base;
    job->merging = true;
}

static void merge_pass_done(LibraryView *view)
{
    ViewPrivate *job = details(view);
    if (!flush_output(view, (uint8_t)(job->bank ^ 1u))) {
        return;
    }
    job->bank ^= 1u;
    job->width *= 2;
    job->base = 0;
    job->page[0] = job->page[1] = UINT32_MAX;
    if (job->width >= view->count) {
        view->state = LIBRARY_VIEW_READY;
    }
}

static void sort_step(LibraryView *view)
{
    ViewPrivate *job = details(view);
    if (!job->merging) {
        if (job->base >= view->count) {
            merge_pass_done(view);
        } else {
            merge_begin(view);
        }
        return;
    }
    if (job->left == job->middle && job->right == job->end) {
        job->base = job->end;
        job->merging = false;
        return;
    }
    uint16_t left = 0;
    uint16_t right = 0;
    bool have_left = job->left < job->middle;
    bool have_right = job->right < job->end;
    if ((have_left && !input_track(view, job->left, 0, &left)) ||
        (have_right && !input_track(view, job->right, 1, &right))) {
        return;
    }
    int compared = have_left && have_right ? compare_tracks(view, left, right) : 0;
    compared = view->config.descending ? -compared : compared;
    if (view->state != LIBRARY_VIEW_SORTING) {
        return;
    }
    bool take_left = have_left && (!have_right || compared <= 0);
    uint16_t chosen = take_left ? left : right;
    if (append_track(view, chosen, job->output_position, (uint8_t)(job->bank ^ 1u))) {
        job->output_position++;
        if (take_left) {
            job->left++;
        } else {
            job->right++;
        }
    }
}

LibraryViewState library_view_step(LibraryView *view)
{
    if (view->state != LIBRARY_VIEW_SCANNING && view->state != LIBRARY_VIEW_SORTING &&
        view->state != LIBRARY_VIEW_READY) {
        return view->state;
    }
    if (!current(view)) {
        view->state = LIBRARY_VIEW_CHANGED;
    } else if (view->state == LIBRARY_VIEW_SCANNING) {
        scan_step(view);
    } else if (view->state == LIBRARY_VIEW_SORTING) {
        sort_step(view);
    }
    return view->state;
}

void library_view_cancel(LibraryView *view)
{
    view->state = LIBRARY_VIEW_CANCELLED;
}

bool library_view_read(const LibraryView *view, uint16_t first, uint16_t count, uint16_t *tracks)
{
    if (view->state != LIBRARY_VIEW_READY || !current(view) ||
        (uint32_t)first + count > view->count || (count != 0 && tracks == NULL)) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    bool reverse = view->config.sort == LIBRARY_VIEW_SORT_ORIGINAL && view->config.descending;
    uint32_t position = reverse ? view->count - (uint32_t)first - count : first;
    uint32_t offset = const_details(view)->bank * BANK_BYTES + position * 2;
    if (!view->work.read(view->work.context, offset, tracks, (uint32_t)count * 2)) {
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        if (tracks[index] >= library_count(view->library, LIBRARY_TRACKS)) {
            return false;
        }
    }
    if (reverse) {
        for (uint32_t index = 0; index < count / 2u; index++) {
            uint16_t swap = tracks[index];
            tracks[index] = tracks[count - 1u - index];
            tracks[count - 1u - index] = swap;
        }
    }
    return true;
}

bool library_view_source_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    return library_view_read(context, first, count, tracks);
}
