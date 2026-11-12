#include "library/cover_build.h"

#include <stddef.h>

#include "core/bytes.h"
#include "core/hash.h"
#include "image/cover_image.h"
#include "library/covers.h"
#include "library/library.h"
#include "library/track_tags.h"
#include "tags_internal.h"

enum {
    ALIGNMENT = 8,
    MAX_FOLDERS = 16,
    STEM_COUNT = 4,
    SUFFIX_COUNT = 3,
    MATCH_CAPACITY = 16,
    FULL_BYTES = COVER_FULL_PIXELS * 2,
    HALF_BYTES = COVER_HALF_PIXELS * 2,
    INDEX_CHUNK = 64,
    STAMP_FIELD = 12,
    STAMP_BYTES = 4,
    SLOT_BYTES = COVERS_ENTRY_SIZE + STAMP_BYTES + COVER_BUILD_RECORD_BYTES,
    STAMP_NUMBERS = 3,
    ART_EMBEDDED = 1,
    ART_FILE = 2,
    BITS_PER_BYTE = 8,
    NO_FOLDER = 0x10000
};

static const char *const STEMS[STEM_COUNT] = {"cover", "folder", "front", "albumart"};
static const char *const SUFFIXES[SUFFIX_COUNT] = {".jpg", ".jpeg", ".png"};

typedef struct CoverEntry {
    uint32_t key;
    uint32_t stamp;
} CoverEntry;

typedef struct CoverLayout {
    uint32_t slots;
    uint32_t stamps_start;
    uint32_t records_start;
} CoverLayout;

typedef struct ArtFile {
    ImageSource image;
    uint32_t stamp;
} ArtFile;

typedef struct CoverMatch {
    char name[MATCH_CAPACITY];
    uint32_t modified;
} CoverMatch;

typedef struct CoverState {
    Library library;
    TrackTags tags;
    TagPictureReader picture;
    ScanEntry entry;
    LibraryAlbum album;
    uint32_t key;
    char path[SCAN_PATH_CAPACITY + 2];
    CoverMatch matches[STEM_COUNT];
    uint16_t record[COVER_FULL_PIXELS + COVER_HALF_PIXELS];
    uint16_t folders[MAX_FOLDERS];
    uint32_t folder_count;
    uint32_t listed_folder;
    CoverMatch listed;
    CoverLayout layout;
    CoverEntry *index;
    uint16_t *slots;
    uint8_t *claimed;
    uint8_t *placed;
    uint8_t *used_slots;
    uint32_t capacity;
    uint32_t count;
    uint32_t kept;
    uint32_t next_slot;
    uint8_t *decode_memory;
    uint32_t decode_size;
    const CoverJob *job;
    ImageSource art;
} CoverState;

static uint8_t *take(uint8_t **cursor, uint8_t *end, uint32_t bytes)
{
    uintptr_t address = (uintptr_t)*cursor;
    uint8_t *start = *cursor + (ALIGNMENT - address % ALIGNMENT) % ALIGNMENT;
    if (start > end || (uint32_t)(end - start) < bytes) {
        return NULL;
    }
    *cursor = start + bytes;
    return start;
}

static void clear(uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        bytes[index] = 0;
    }
}

static uint32_t bit_bytes(uint32_t bits)
{
    return (bits + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
}

static bool bit_set(const uint8_t *bits, uint32_t index)
{
    return (bits[index / BITS_PER_BYTE] >> (index % BITS_PER_BYTE) & 1u) != 0;
}

static void set_bit(uint8_t *bits, uint32_t index)
{
    bits[index / BITS_PER_BYTE] |= (uint8_t)(1u << (index % BITS_PER_BYTE));
}

static CoverLayout layout_for(uint32_t capacity)
{
    uint32_t room = capacity > COVERS_HEADER_SIZE ? capacity - COVERS_HEADER_SIZE : 0;
    uint32_t slots = room / SLOT_BYTES;
    slots = slots < COVER_BUILD_MAX_COVERS ? slots : COVER_BUILD_MAX_COVERS;
    uint32_t stamps_start = COVERS_HEADER_SIZE + slots * COVERS_ENTRY_SIZE;
    return (CoverLayout){slots, stamps_start, stamps_start + slots * STAMP_BYTES};
}

static uint32_t index_capacity(CoverLayout layout, uint32_t albums, uint32_t previous)
{
    uint32_t limit = layout.slots < albums ? layout.slots : albums;
    return limit > previous ? limit : previous;
}

static CoverState *prepare(const CoverJob *job, uint32_t albums, CoverLayout layout,
                           uint32_t previous)
{
    uint8_t *cursor = job->memory;
    uint8_t *end = cursor + job->size;
    uint32_t capacity = index_capacity(layout, albums, previous);
    CoverState *state = (CoverState *)take(&cursor, end, sizeof(CoverState));
    CoverEntry *index = (CoverEntry *)take(&cursor, end, capacity * sizeof(CoverEntry) + 1);
    uint16_t *slots = (uint16_t *)take(&cursor, end, capacity * sizeof(uint16_t) + 1);
    uint8_t *claimed = take(&cursor, end, bit_bytes(capacity) + 1);
    uint8_t *placed = take(&cursor, end, bit_bytes(capacity) + 1);
    uint8_t *used_slots = take(&cursor, end, bit_bytes(layout.slots) + 1);
    if (state == NULL || index == NULL || slots == NULL || claimed == NULL || placed == NULL ||
        used_slots == NULL) {
        return NULL;
    }
    clear(placed, bit_bytes(capacity));
    clear(used_slots, bit_bytes(layout.slots));
    state->layout = layout;
    state->index = index;
    state->slots = slots;
    state->claimed = claimed;
    state->placed = placed;
    state->used_slots = used_slots;
    state->capacity = capacity;
    state->count = 0;
    state->kept = 0;
    state->next_slot = 0;
    state->listed_folder = NO_FOLDER;
    state->decode_memory = cursor;
    state->decode_size = (uint32_t)(end - cursor);
    return state;
}

static bool write_bytes(const CoverJob *job, uint32_t offset, const void *bytes, uint32_t length)
{
    return job->sink.write(job->sink.context, offset, bytes, length);
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static uint32_t art_stamp(const char *path, uint32_t first, uint32_t second, uint32_t third)
{
    uint8_t numbers[STAMP_NUMBERS * 4];
    bytes_put_le32(numbers, first);
    bytes_put_le32(numbers + 4, second);
    bytes_put_le32(numbers + 8, third);
    uint32_t hash = hash_update(HASH_START, path, text_length(path));
    return hash_update(hash, numbers, sizeof numbers);
}

static bool slot_of(const CoverState *state, uint32_t offset, uint32_t *slot)
{
    const CoverLayout *layout = &state->layout;
    if (offset < layout->records_start) {
        return false;
    }
    uint32_t delta = offset - layout->records_start;
    *slot = delta / COVER_BUILD_RECORD_BYTES;
    return delta % COVER_BUILD_RECORD_BYTES == 0 && *slot < layout->slots;
}

static uint32_t offset_of(const CoverState *state, uint32_t position)
{
    return state->layout.records_start + state->slots[position] * COVER_BUILD_RECORD_BYTES;
}

static bool free_slot(CoverState *state, uint32_t *slot)
{
    while (state->next_slot < state->layout.slots && bit_set(state->used_slots, state->next_slot)) {
        state->next_slot++;
    }
    *slot = state->next_slot;
    return state->next_slot < state->layout.slots;
}

static void move_entry(CoverState *state, uint32_t from, uint32_t to)
{
    state->index[to] = state->index[from];
    state->slots[to] = state->slots[from];
}

static uint32_t previous_count(const CoverJob *job, CoverLayout layout)
{
    uint8_t header[COVERS_HEADER_SIZE];
    Covers covers;
    if (job->previous.read == NULL || !covers_open(&covers, job->previous, job->capacity) ||
        !job->previous.read(job->previous.context, 0, header, sizeof header)) {
        return 0;
    }
    bool stamped = bytes_le32(header + STAMP_FIELD) == layout.stamps_start;
    return stamped && covers.count <= layout.slots ? covers.count : 0;
}

static bool accept_entry(CoverState *state, CoverEntry entry, uint32_t offset)
{
    uint32_t slot = 0;
    bool ordered = state->count == 0 || state->index[state->count - 1].key < entry.key;
    if (!ordered || !slot_of(state, offset, &slot) || bit_set(state->used_slots, slot)) {
        return false;
    }
    set_bit(state->used_slots, slot);
    state->slots[state->count] = (uint16_t)slot;
    state->index[state->count++] = entry;
    return true;
}

static bool load_chunk(CoverState *state, const CoverJob *job, uint32_t first, uint32_t count)
{
    uint8_t entries[INDEX_CHUNK * COVERS_ENTRY_SIZE];
    uint8_t stamps[INDEX_CHUNK * STAMP_BYTES];
    ByteSource source = job->previous;
    uint32_t entries_at = COVERS_HEADER_SIZE + first * COVERS_ENTRY_SIZE;
    uint32_t stamps_at = state->layout.stamps_start + first * STAMP_BYTES;
    if (!source.read(source.context, entries_at, entries, count * COVERS_ENTRY_SIZE) ||
        !source.read(source.context, stamps_at, stamps, count * STAMP_BYTES)) {
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        const uint8_t *entry = entries + index * COVERS_ENTRY_SIZE;
        CoverEntry loaded = {bytes_le32(entry), bytes_le32(stamps + index * STAMP_BYTES)};
        if (!accept_entry(state, loaded, bytes_le32(entry + 4))) {
            return false;
        }
    }
    return true;
}

static void load_previous(CoverState *state, const CoverJob *job, uint32_t previous)
{
    bool loaded = true;
    for (uint32_t first = 0; loaded && first < previous; first += INDEX_CHUNK) {
        uint32_t count = previous - first < INDEX_CHUNK ? previous - first : INDEX_CHUNK;
        loaded = load_chunk(state, job, first, count);
    }
    state->count = loaded ? state->count : 0;
    state->kept = state->count;
    clear(state->used_slots, bit_bytes(state->layout.slots));
    clear(state->claimed, bit_bytes(state->count));
}

static bool find_kept(const CoverState *state, uint32_t key, uint32_t *position)
{
    uint32_t low = 0;
    uint32_t high = state->kept;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        uint32_t found = state->index[middle].key;
        if (found == key) {
            *position = middle;
            return true;
        }
        low = found < key ? middle + 1 : low;
        high = found < key ? high : middle;
    }
    return false;
}

static void drop_unclaimed(CoverState *state)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < state->count; index++) {
        if (bit_set(state->claimed, index)) {
            move_entry(state, index, count++);
        }
    }
    clear(state->claimed, bit_bytes(state->count));
    state->count = count;
}

static void keep_present(CoverState *state, uint32_t albums)
{
    for (uint32_t album_index = 0; album_index < albums && state->kept > 0; album_index++) {
        uint32_t position = 0;
        if (find_kept(state, library_album_key(&state->library, album_index), &position)) {
            set_bit(state->claimed, position);
        }
    }
    drop_unclaimed(state);
    state->kept = state->count;
    for (uint32_t index = 0; index < state->count; index++) {
        set_bit(state->used_slots, state->slots[index]);
        set_bit(state->claimed, index);
    }
}

static void unclaim(CoverState *state, uint32_t key)
{
    uint32_t position = 0;
    if (find_kept(state, key, &position)) {
        state->claimed[position / BITS_PER_BYTE] &= (uint8_t)~(1u << (position % BITS_PER_BYTE));
        set_bit(state->placed, position);
    }
}

static bool read_track_path(CoverState *state, uint32_t track_index, LibraryTrack *track)
{
    state->path[0] = '/';
    return library_read_track(&state->library, track_index, track) &&
           library_read_string(&state->library, track->path, state->path + 1,
                               sizeof state->path - 1);
}

static bool open_embedded(CoverState *state, const CoverJob *job, ArtFile *art)
{
    LibraryTrack track;
    ByteSource source;
    uint32_t size = 0;
    if (!read_track_path(state, state->album.first_track, &track) ||
        !job->files.open_file(job->files.context, state->path, &source, &size)) {
        return false;
    }
    TagFormat format = track_tags_format_for_name(state->path);
    if (!track_tags_read(source, size, format, &state->tags) ||
        !tag_picture_open(&state->picture, source, size, &state->tags.picture, &art->image)) {
        job->files.close_file(job->files.context);
        return false;
    }
    art->stamp = art_stamp(state->path, size, art->image.length, ART_EMBEDDED);
    return true;
}

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static bool is_name(const char *name, const char *stem, const char *suffix)
{
    while (*stem != '\0') {
        if (lower(*name++) != *stem++) {
            return false;
        }
    }
    while (*suffix != '\0') {
        if (lower(*name++) != *suffix++) {
            return false;
        }
    }
    return *name == '\0';
}

static bool before(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (uint8_t)*left < (uint8_t)*right;
}

static void copy_name(char *target, const char *name)
{
    uint32_t index = 0;
    while (name[index] != '\0' && index + 1 < MATCH_CAPACITY) {
        target[index] = name[index];
        index++;
    }
    target[index] = '\0';
}

static void consider(CoverState *state, const ScanEntry *entry)
{
    for (uint32_t stem = 0; stem < STEM_COUNT; stem++) {
        CoverMatch *match = &state->matches[stem];
        for (uint32_t suffix = 0; suffix < SUFFIX_COUNT; suffix++) {
            if (is_name(entry->name, STEMS[stem], SUFFIXES[suffix]) &&
                (match->name[0] == '\0' || before(entry->name, match->name))) {
                copy_name(match->name, entry->name);
                match->modified = entry->modified;
            }
        }
    }
}

static uint32_t folder_length(const char *path)
{
    uint32_t slash = 0;
    for (uint32_t index = 0; path[index] != '\0'; index++) {
        slash = path[index] == '/' ? index : slash;
    }
    return slash;
}

static void list_folder(CoverState *state, const CoverJob *job)
{
    for (uint32_t stem = 0; stem < STEM_COUNT; stem++) {
        state->matches[stem].name[0] = '\0';
    }
    if (!job->files.open_dir(job->files.context, state->path)) {
        return;
    }
    while (job->files.next_entry(job->files.context, &state->entry)) {
        if (!state->entry.directory) {
            consider(state, &state->entry);
        }
    }
    job->files.close_dir(job->files.context);
}

static void pick_match(CoverState *state)
{
    state->listed.name[0] = '\0';
    for (uint32_t stem = 0; stem < STEM_COUNT && state->listed.name[0] == '\0'; stem++) {
        state->listed = state->matches[stem];
    }
}

static const CoverMatch *best_match(CoverState *state, const CoverJob *job, uint32_t folder)
{
    uint32_t length = folder_length(state->path);
    state->path[length > 0 ? length : 1] = '\0';
    if (state->listed_folder != folder) {
        list_folder(state, job);
        pick_match(state);
        state->listed_folder = folder;
    }
    return state->listed.name[0] != '\0' ? &state->listed : NULL;
}

static bool append_name(CoverState *state, const char *name)
{
    uint32_t length = text_length(state->path);
    if (length > 1) {
        state->path[length++] = '/';
    }
    for (uint32_t index = 0; name[index] != '\0'; index++) {
        if (length + 1 >= sizeof state->path) {
            return false;
        }
        state->path[length++] = name[index];
    }
    state->path[length] = '\0';
    return true;
}

static bool open_art_file(CoverState *state, const CoverJob *job, CoverMatch match, ArtFile *art)
{
    uint32_t size = 0;
    ByteSource source;
    if (!append_name(state, match.name) ||
        !job->files.open_file(job->files.context, state->path, &source, &size)) {
        return false;
    }
    art->image = (ImageSource){source.read, source.context, 0, size};
    art->stamp = art_stamp(state->path, size, match.modified, ART_FILE);
    return true;
}

static bool folder_tried(CoverState *state, uint16_t folder)
{
    for (uint32_t index = 0; index < state->folder_count; index++) {
        if (state->folders[index] == folder) {
            return true;
        }
    }
    if (state->folder_count < MAX_FOLDERS) {
        state->folders[state->folder_count++] = folder;
    }
    return false;
}

static bool open_folder_art(CoverState *state, const CoverJob *job, ArtFile *art)
{
    const LibraryAlbum *album = &state->album;
    state->folder_count = 0;
    for (uint32_t track = 0; track < album->track_count && state->folder_count < MAX_FOLDERS;
         track++) {
        LibraryTrack info;
        if (!read_track_path(state, album->first_track + track, &info) ||
            folder_tried(state, info.folder)) {
            continue;
        }
        const CoverMatch *match = best_match(state, job, info.folder);
        if (match != NULL) {
            return open_art_file(state, job, *match, art);
        }
    }
    return false;
}

static bool stop_requested(const CoverJob *job)
{
    return job->stop.requested != NULL && job->stop.requested(job->stop.context);
}

static bool read_unless_stopped(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const CoverState *state = context;
    return !stop_requested(state->job) &&
           state->art.read(state->art.context, offset, buffer, length);
}

static bool decode_art(CoverState *state, const ArtFile *art)
{
    state->art = art->image;
    ImageSource image = {read_unless_stopped, state, art->image.offset, art->image.length};
    return cover_image_decode(image, state->decode_memory, state->decode_size, state->record,
                              state->record + COVER_FULL_PIXELS) == COVER_IMAGE_OK;
}

static void pack_pixels(uint16_t *pixels, uint32_t count)
{
    uint8_t *bytes = (uint8_t *)pixels;
    for (uint32_t index = 0; index < count; index++) {
        uint16_t value = pixels[index];
        bytes_put_le16(bytes + index * 2, value);
    }
}

static bool write_record(CoverState *state, const CoverJob *job, uint32_t offset)
{
    pack_pixels(state->record, COVER_FULL_PIXELS + COVER_HALF_PIXELS);
    return write_bytes(job, offset, state->record, COVER_BUILD_RECORD_BYTES);
}

static bool record_slot(CoverState *state, bool known, uint32_t position, uint32_t *slot)
{
    if (known) {
        *slot = state->slots[position];
        return true;
    }
    return state->count < state->capacity && free_slot(state, slot);
}

static uint32_t append_entry(CoverState *state, uint32_t slot)
{
    uint32_t position = state->count++;
    state->slots[position] = (uint16_t)slot;
    set_bit(state->used_slots, slot);
    return position;
}

typedef enum Placement {
    PLACED,
    PLACE_FAILED,
    PLACE_UNREADABLE
} Placement;

static Placement place_cover(CoverState *state, const CoverJob *job, const ArtFile *art,
                             CoverStats *stats)
{
    uint32_t position = 0;
    uint32_t slot = 0;
    bool known = find_kept(state, state->key, &position);
    uint32_t stored = known ? state->index[position].stamp : 0;
    if (stored != 0 && stored == art->stamp) {
        set_bit(state->placed, position);
        stats->reused++;
        return PLACED;
    }
    if (!record_slot(state, known, position, &slot)) {
        return PLACE_FAILED;
    }
    if (!decode_art(state, art)) {
        return PLACE_UNREADABLE;
    }
    if (!write_record(state, job, state->layout.records_start + slot * COVER_BUILD_RECORD_BYTES)) {
        return PLACE_FAILED;
    }
    position = known ? position : append_entry(state, slot);
    state->index[position] = (CoverEntry){state->key, art->stamp};
    set_bit(state->claimed, position);
    set_bit(state->placed, position);
    return PLACED;
}

static Placement place_opened(CoverState *state, const CoverJob *job, const ArtFile *art,
                              CoverStats *stats)
{
    Placement placement = place_cover(state, job, art, stats);
    job->files.close_file(job->files.context);
    return placement;
}

static ScanResult cover_album(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    ArtFile art;
    Placement placement = PLACE_UNREADABLE;
    if (state->album.track_count > 0 && open_embedded(state, job, &art)) {
        placement = place_opened(state, job, &art, stats);
    }
    if (placement == PLACE_UNREADABLE && state->album.track_count > 0 &&
        open_folder_art(state, job, &art)) {
        placement = place_opened(state, job, &art, stats);
    }
    if (placement != PLACED && !stop_requested(job)) {
        unclaim(state, state->key);
    }
    stats->missing += placement == PLACED ? 0u : 1u;
    return SCAN_OK;
}

static bool already_placed(const CoverState *state, uint32_t key)
{
    uint32_t position = 0;
    if (find_kept(state, key, &position)) {
        return bit_set(state->placed, position);
    }
    for (uint32_t index = state->kept; index < state->count; index++) {
        if (state->index[index].key == key) {
            return true;
        }
    }
    return false;
}

static void sort_index(CoverState *state)
{
    for (uint32_t index = 1; index < state->count; index++) {
        CoverEntry entry = state->index[index];
        uint16_t slot = state->slots[index];
        uint32_t position = index;
        while (position > 0 && state->index[position - 1].key > entry.key) {
            move_entry(state, position - 1, position);
            position--;
        }
        state->index[position] = entry;
        state->slots[position] = slot;
    }
}

static void pack_entry(const CoverState *state, uint32_t position, uint8_t *bytes, bool stamps)
{
    if (stamps) {
        bytes_put_le32(bytes, state->index[position].stamp);
        return;
    }
    bytes_put_le32(bytes, state->index[position].key);
    bytes_put_le32(bytes + 4, offset_of(state, position));
}

static bool write_table(const CoverState *state, const CoverJob *job, bool stamps)
{
    uint8_t chunk[INDEX_CHUNK * COVERS_ENTRY_SIZE];
    uint32_t width = stamps ? STAMP_BYTES : COVERS_ENTRY_SIZE;
    uint32_t start = stamps ? state->layout.stamps_start : COVERS_HEADER_SIZE;
    for (uint32_t first = 0; first < state->count; first += INDEX_CHUNK) {
        uint32_t count = state->count - first < INDEX_CHUNK ? state->count - first : INDEX_CHUNK;
        for (uint32_t index = 0; index < count; index++) {
            pack_entry(state, first + index, chunk + index * width, stamps);
        }
        if (!write_bytes(job, start + first * width, chunk, count * width)) {
            return false;
        }
    }
    return true;
}

static bool clear_header(const CoverJob *job)
{
    uint8_t header[COVERS_HEADER_SIZE] = {0};
    return write_bytes(job, 0, header, sizeof header);
}

static bool write_header(const CoverState *state, const CoverJob *job)
{
    uint8_t header[COVERS_HEADER_SIZE] = {'D', 'S', 'C', 'V'};
    bytes_put_le16(header + 4, COVERS_VERSION);
    bytes_put_le16(header + 6, state->count);
    bytes_put_le16(header + 8, COVER_FULL_SIZE);
    bytes_put_le16(header + 10, COVER_HALF_SIZE);
    bytes_put_le32(header + STAMP_FIELD, state->layout.stamps_start);
    return write_bytes(job, 0, header, sizeof header);
}

static void report(const CoverJob *job, uint32_t done, uint32_t total)
{
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, done, total);
    }
}

static ScanResult build_albums(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    for (uint32_t album_index = job->first_album; album_index < stats->albums; album_index++) {
        report(job, album_index, stats->albums);
        stats->next_album = album_index;
        if (stop_requested(job)) {
            return SCAN_STOPPED;
        }
        state->key = library_album_key(&state->library, album_index);
        if (!library_read_album(&state->library, album_index, &state->album) ||
            already_placed(state, state->key)) {
            continue;
        }
        ScanResult result = cover_album(state, job, stats);
        if (result != SCAN_OK) {
            return result;
        }
    }
    report(job, stats->albums, stats->albums);
    stats->next_album = stats->albums;
    return SCAN_OK;
}

static ScanResult finish(CoverState *state, const CoverJob *job, CoverStats *stats,
                         ScanResult result)
{
    drop_unclaimed(state);
    sort_index(state);
    stats->covers = state->count;
    bool keep = result == SCAN_OK || result == SCAN_STOPPED;
    if (keep && (!clear_header(job) || !write_table(state, job, false) ||
                 !write_table(state, job, true) || !write_header(state, job))) {
        result = SCAN_WRITE_FAILED;
    }
    return result;
}

static CoverState *start(const CoverJob *job, uint32_t albums, CoverLayout layout)
{
    uint32_t previous = previous_count(job, layout);
    CoverState *state = prepare(job, albums, layout, previous);
    if (state == NULL && previous > 0) {
        previous = 0;
        state = prepare(job, albums, layout, previous);
    }
    if (state != NULL) {
        load_previous(state, job, previous);
    }
    return state;
}

ScanResult cover_build(const CoverJob *job, CoverStats *stats)
{
    *stats = (CoverStats){.albums = 0, .covers = 0, .reused = 0, .missing = 0, .next_album = 0};
    Library probe;
    if (library_open(&probe, job->library, job->library_size) != LIBRARY_OK) {
        return SCAN_NO_MUSIC_ROOT;
    }
    stats->albums = library_count(&probe, LIBRARY_ALBUMS);
    CoverLayout layout = layout_for(job->capacity);
    CoverState *state = start(job, stats->albums, layout);
    if (state == NULL) {
        return SCAN_OUT_OF_MEMORY;
    }
    if (layout.slots == 0 && stats->albums > 0) {
        return SCAN_NO_ROOM;
    }
    state->library = probe;
    state->job = job;
    keep_present(state, stats->albums);
    return finish(state, job, stats, build_albums(state, job, stats));
}
