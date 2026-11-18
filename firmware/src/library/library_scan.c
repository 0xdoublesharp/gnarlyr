#include "library/library_scan.h"

#include <stddef.h>
#include <stdint.h>

#include "core/drive_path.h"
#include "scan_internal.h"

enum {
    LISTING_SHARE = 8,
    MINIMUM_LISTING = 16 * 1024,
    MAXIMUM_LISTING = 96 * 1024,
    ALIGNMENT = 8,
    LISTING_ALIGNMENT = 4,
    RESUME_BYTES = (2 + SCAN_NAME_CAPACITY + 3) & ~3,
    NO_RESUME = 0xFFFF,
    MINIMUM_BATCH = 2,
    ENTRY_OVERHEAD = 8,
    PLACE_BYTES = sizeof(ScanPlace)
};

typedef struct Listing {
    uint32_t *offsets;
    uint32_t *scratch;
} Listing;

uint32_t scan_text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

const char *scan_entry_name(const ScanState *state, uint32_t offset)
{
    return (const char *)state->listing + offset + 1;
}

static bool entry_is_directory(const ScanState *state, uint32_t offset)
{
    return state->listing[offset] == ENTRY_DIRECTORY;
}

static uint32_t entry_bytes(const ScanState *state, uint32_t offset, bool placed)
{
    uint32_t place = placed && entry_is_directory(state, offset) ? PLACE_BYTES : 0;
    return 2 + scan_text_length(scan_entry_name(state, offset)) + place;
}

static int compare_texts(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static int compare_names(const ScanState *state, uint32_t left, uint32_t right)
{
    return compare_texts(scan_entry_name(state, left), scan_entry_name(state, right));
}

static void merge_names(const ScanState *state, const uint32_t *from, uint32_t *to, uint32_t start,
                        uint32_t middle, uint32_t end)
{
    uint32_t left = start;
    uint32_t right = middle;
    for (uint32_t out = start; out < end; out++) {
        bool take_left =
            left < middle && (right >= end || compare_names(state, from[left], from[right]) <= 0);
        to[out] = take_left ? from[left++] : from[right++];
    }
}

static void sort_names(const ScanState *state, uint32_t *items, uint32_t *scratch, uint32_t count)
{
    uint32_t *from = items;
    uint32_t *to = scratch;
    for (uint32_t width = 1; width < count; width *= 2) {
        for (uint32_t start = 0; start < count; start += 2 * width) {
            uint32_t middle = start + width < count ? start + width : count;
            uint32_t end = start + 2 * width < count ? start + 2 * width : count;
            merge_names(state, from, to, start, middle, end);
        }
        uint32_t *swap = from;
        from = to;
        to = swap;
    }
    for (uint32_t index = 0; from != items && index < count; index++) {
        items[index] = from[index];
    }
}

static bool store_entry(ScanState *state, const ScanEntry *entry)
{
    uint32_t length = scan_text_length(entry->name);
    uint32_t place = entry->directory && state->placing ? PLACE_BYTES : 0;
    if (length + 2 + place > state->playlist_floor - state->listing_used) {
        return false;
    }
    uint8_t *record = state->listing + state->listing_used;
    record[0] = entry->directory ? ENTRY_DIRECTORY : ENTRY_PLAIN;
    for (uint32_t index = 0; index <= length; index++) {
        record[1 + index] = (uint8_t)entry->name[index];
    }
    const uint8_t *bytes = (const uint8_t *)&entry->place;
    for (uint32_t index = 0; index < place; index++) {
        record[2 + length + index] = bytes[index];
    }
    state->listing_used += length + 2 + place;
    return true;
}

static uint32_t align4(uint32_t value)
{
    return (value + 3) & ~3u;
}

static uint32_t *sorted_offsets(ScanState *state, uint32_t start, uint32_t count)
{
    uint32_t table = align4(state->listing_used);
    uint32_t *offsets = (uint32_t *)(void *)(state->listing + table);
    uint32_t cursor = start;
    for (uint32_t index = 0; index < count; index++) {
        offsets[index] = cursor;
        cursor += entry_bytes(state, cursor, state->placing);
    }
    sort_names(state, offsets, offsets + count, count);
    return offsets;
}

static ScanResult index_listing(ScanState *state, uint32_t start, uint32_t count, Level *level)
{
    uint32_t table = align4(state->listing_used);
    if (table > state->playlist_floor || (uint64_t)count * 8 > state->playlist_floor - table) {
        return SCAN_FOLDER_TOO_BIG;
    }
    sorted_offsets(state, start, count);
    state->listing_used = table + count * 4;
    level->first = table;
    level->count = count;
    level->next = 0;
    level->path_length = state->path_length;
    level->placed = state->placing;
    return SCAN_OK;
}

static ScanResult read_whole(ScanState *state, const ScanFileSystem *files, Level *level,
                             bool *fits)
{
    if (!files->open_dir(files->context, state->path, &level->place)) {
        return SCAN_NO_MUSIC_ROOT;
    }
    uint32_t start = state->listing_used;
    uint32_t count = 0;
    *fits = true;
    while (*fits && files->next_entry(files->context, &state->entry)) {
        if (state->entry.name[0] == '.') {
            continue;
        }
        *fits = store_entry(state, &state->entry) &&
                align4(state->listing_used) + (count + 1) * ENTRY_OVERHEAD <= state->playlist_floor;
        count++;
    }
    files->close_dir(files->context);
    if (!*fits) {
        state->listing_used = start;
        return SCAN_OK;
    }
    level->start = start;
    level->partial = false;
    return index_listing(state, start, count, level);
}

typedef struct Batch {
    uint32_t start;
    uint32_t end;
    uint32_t count;
    bool cut;
    const char *after;
} Batch;

static bool batch_fits(const ScanState *state, const Batch *batch, uint32_t length)
{
    uint64_t need = (uint64_t)align4(state->listing_used + length + 2) +
                    (uint64_t)(batch->count + 1) * ENTRY_OVERHEAD;
    return need <= batch->end;
}

static void keep_smaller_half(ScanState *state, Batch *batch)
{
    uint32_t *offsets = sorted_offsets(state, batch->start, batch->count);
    uint32_t keep = batch->count / 2;
    const char *first_dropped = scan_entry_name(state, offsets[keep]);
    uint32_t length = scan_text_length(first_dropped);
    for (uint32_t index = 0; index <= length; index++) {
        state->cutoff[index] = first_dropped[index];
    }
    uint32_t write = batch->start;
    for (uint32_t read = batch->start; read < state->listing_used;) {
        uint32_t size = entry_bytes(state, read, state->placing);
        if (compare_texts(scan_entry_name(state, read), state->cutoff) < 0) {
            for (uint32_t index = 0; index < size; index++) {
                state->listing[write + index] = state->listing[read + index];
            }
            write += size;
        }
        read += size;
    }
    state->listing_used = write;
    batch->count = keep;
    batch->cut = true;
}

static bool wanted(const ScanState *state, const Batch *batch, const char *name)
{
    return name[0] != '.' && (batch->after == NULL || compare_texts(name, batch->after) > 0) &&
           (!batch->cut || compare_texts(name, state->cutoff) < 0);
}

static ScanResult add_to_batch(ScanState *state, Batch *batch)
{
    uint32_t length = scan_text_length(state->entry.name);
    while (!batch_fits(state, batch, length)) {
        if (batch->count < MINIMUM_BATCH) {
            return SCAN_FOLDER_TOO_BIG;
        }
        keep_smaller_half(state, batch);
        if (compare_texts(state->entry.name, state->cutoff) >= 0) {
            return SCAN_OK;
        }
    }
    store_entry(state, &state->entry);
    batch->count++;
    return SCAN_OK;
}

static ScanResult read_batch(ScanState *state, const ScanFileSystem *files, Level *level,
                             uint32_t start, bool resume)
{
    uint8_t *header = state->listing + start;
    if (!resume) {
        header[0] = (uint8_t)NO_RESUME;
        header[1] = (uint8_t)(NO_RESUME >> 8);
    }
    bool has_after = (uint32_t)(header[0] | header[1] << 8) != NO_RESUME;
    Batch batch = {.start = start + RESUME_BYTES,
                   .end = start + (state->playlist_floor - start) / 2,
                   .count = 0,
                   .cut = false,
                   .after = has_after ? (const char *)header + 2 : NULL};
    if (batch.end < batch.start + 2 * (RESUME_BYTES + ENTRY_OVERHEAD)) {
        return SCAN_FOLDER_TOO_BIG;
    }
    if (!files->open_dir(files->context, state->path, &level->place)) {
        return SCAN_NO_MUSIC_ROOT;
    }
    state->placing = false;
    state->listing_used = batch.start;
    ScanResult result = SCAN_OK;
    while (result == SCAN_OK && files->next_entry(files->context, &state->entry)) {
        if (wanted(state, &batch, state->entry.name)) {
            result = add_to_batch(state, &batch);
        }
    }
    files->close_dir(files->context);
    if (result != SCAN_OK) {
        return result;
    }
    level->start = start;
    level->partial = batch.cut;
    return index_listing(state, batch.start, batch.count, level);
}

static ScanResult read_entries(ScanState *state, const ScanFileSystem *files, Level *level,
                               uint32_t pass)
{
    bool fits = false;
    uint32_t start = state->listing_used;
    ScanResult result = SCAN_OK;
    for (; result == SCAN_OK && !fits && pass < 2; pass++) {
        state->placing = pass == 0;
        result = read_whole(state, files, level, &fits);
    }
    if (result != SCAN_OK || fits) {
        return result;
    }
    return read_batch(state, files, level, start, false);
}

static ScanResult read_following_batch(ScanState *state, const ScanFileSystem *files, Level *level)
{
    const uint32_t *offsets = (const uint32_t *)(const void *)(state->listing + level->first);
    const char *last = scan_entry_name(state, offsets[level->count - 1]);
    uint32_t length = scan_text_length(last);
    char *resume = (char *)state->listing + level->start + 2;
    for (uint32_t index = 0; index <= length; index++) {
        resume[index] = last[index];
    }
    state->listing[level->start] = (uint8_t)length;
    state->listing[level->start + 1] = (uint8_t)(length >> 8);
    return read_batch(state, files, level, level->start, true);
}

static ScanResult read_listing(ScanState *state, const ScanFileSystem *files, Level *level,
                               uint32_t pass)
{
    ScanResult result = read_entries(state, files, level, pass);
    return result == SCAN_OK ? scan_mark_cue_targets(state, files, level) : result;
}

static ScanResult read_next_batch(ScanState *state, const ScanFileSystem *files, Level *level)
{
    ScanResult result = read_following_batch(state, files, level);
    return result == SCAN_OK ? scan_mark_cue_targets(state, files, level) : result;
}

bool scan_append_path(ScanState *state, const char *name, bool directory)
{
    uint32_t length = scan_text_length(name);
    if (state->path_length + length + 2 > SCAN_PATH_CAPACITY) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        state->path[state->path_length + index] = name[index];
    }
    state->path_length += length;
    if (directory) {
        state->path[state->path_length++] = '/';
    }
    state->path[state->path_length] = '\0';
    return true;
}

void scan_truncate_path(ScanState *state, uint32_t length)
{
    state->path_length = length;
    state->path[length] = '\0';
}

ScanResult scan_builder_result(BuilderResult result)
{
    switch (result) {
    case BUILDER_OK:
        return SCAN_OK;
    case BUILDER_OUT_OF_MEMORY:
        return SCAN_OUT_OF_MEMORY;
    case BUILDER_TOO_MANY:
        return SCAN_TOO_MANY;
    case BUILDER_WRITE_FAILED:
        return SCAN_WRITE_FAILED;
    case BUILDER_NO_ROOM:
        return SCAN_NO_ROOM;
    }
    return SCAN_WRITE_FAILED;
}

void scan_report(const ScanJob *job, const ScanStats *stats)
{
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, stats->files, stats->tracks);
    }
}

static ScanResult add_read_file(ScanState *state, const ScanJob *job, ScanStats *stats,
                                ByteSource source, uint32_t size)
{
    TagFormat format = track_tags_format_for_name(state->path);
    if (!track_tags_read(source, size, format, &state->tags)) {
        stats->skipped++;
        return SCAN_OK;
    }
    track_fields_from_tags(state->path + 1, &state->tags, &state->fields);
    if (scan_is_chaptered_name(state->path) &&
        mp4_chapters_open(&state->chapters, source, size, state->tags.duration_ms)) {
        return scan_add_chapters(state, job, stats);
    }
    ScanResult result = scan_add_track(state, &state->fields.track);
    stats->tracks += result == SCAN_OK;
    return result;
}

static ScanResult add_file(ScanState *state, const ScanJob *job, ScanStats *stats)
{
    ByteSource source;
    uint32_t size = 0;
    ScanResult result = SCAN_OK;
    stats->files++;
    if (job->files.open_file(job->files.context, state->path, &source, &size)) {
        result = add_read_file(state, job, stats, source, size);
        job->files.close_file(job->files.context);
    } else {
        stats->skipped++;
    }
    scan_report(job, stats);
    return result;
}

static bool is_playlist_name(const char *name)
{
    uint32_t length = scan_text_length(name);
    const char *dot = NULL;
    for (uint32_t index = 1; index < length; index++) {
        dot = name[index] == '.' ? name + index : dot;
    }
    if (dot == NULL) {
        return false;
    }
    char lower[6] = {0};
    uint32_t count = 0;
    for (const char *cursor = dot + 1; *cursor != '\0' && count < 5; cursor++) {
        lower[count++] = (char)(*cursor >= 'A' && *cursor <= 'Z' ? *cursor - 'A' + 'a' : *cursor);
    }
    bool m3u = count == 3 && lower[0] == 'm' && lower[1] == '3' && lower[2] == 'u';
    bool m3u8 =
        count == 4 && lower[0] == 'm' && lower[1] == '3' && lower[2] == 'u' && lower[3] == '8';
    return (m3u || m3u8) && dot[count + 1] == '\0';
}

static ScanResult note_playlist(ScanState *state, const char *name)
{
    uint32_t parent = state->path_length;
    uint32_t length = parent + scan_text_length(name);
    uint32_t need = align4(length + 1 + 4);
    if (need > state->playlist_floor - align4(state->listing_used)) {
        return SCAN_FOLDER_TOO_BIG;
    }
    state->playlist_floor -= need;
    uint8_t *record = state->listing + state->playlist_floor;
    record[0] = (uint8_t)(length & 0xFF);
    record[1] = (uint8_t)(length >> 8);
    record[2] = (uint8_t)(parent & 0xFF);
    record[3] = (uint8_t)(parent >> 8);
    for (uint32_t index = 0; index < parent; index++) {
        record[4 + index] = (uint8_t)state->path[index];
    }
    for (uint32_t index = parent; index < length; index++) {
        record[4 + index] = (uint8_t)name[index - parent];
    }
    record[4 + length] = 0;
    state->playlist_count++;
    return SCAN_OK;
}

static uint32_t first_placed(const ScanState *state)
{
    uint32_t index = 0;
    while (index < state->depth && !state->levels[index].placed) {
        index++;
    }
    return index;
}

static ScanResult unplace_path(ScanState *state, const ScanFileSystem *files, uint32_t first)
{
    state->listing_used = state->levels[first].start;
    ScanResult result = SCAN_OK;
    for (uint32_t index = first; result == SCAN_OK && index <= state->depth; index++) {
        Level *level = &state->levels[index];
        uint32_t next = level->next;
        scan_truncate_path(state, level->path_length);
        result = read_listing(state, files, level, 1);
        level->next = next;
        if (result == SCAN_OK && index < state->depth) {
            const uint32_t *offsets =
                (const uint32_t *)(const void *)(state->listing + level->first);
            (void)scan_append_path(state, scan_entry_name(state, offsets[next - 1]), true);
        }
    }
    return result;
}

static bool shadowed(ScanState *state, const ScanJob *job, const char *name)
{
    if (job->filter.shadowed == NULL || state->path[1] != DRIVE_PATH_INTERNAL ||
        state->path[2] != '/' || !scan_append_path(state, name, false)) {
        return false;
    }
    bool found = job->filter.shadowed(job->filter.context, state->path + 2);
    scan_truncate_path(state, state->levels[state->depth - 1].path_length);
    return found;
}

static bool skipped_file(ScanState *state, const ScanJob *job, uint32_t offset, bool cue)
{
    const char *name = scan_entry_name(state, offset);
    return state->listing[offset] == ENTRY_CLAIMED ||
           (!cue && track_tags_format_for_name(name) == TAG_FORMAT_UNKNOWN) ||
           shadowed(state, job, name);
}

static ScanResult visit_entry(ScanState *state, const ScanJob *job, ScanStats *stats,
                              uint32_t offset)
{
    const char *name = scan_entry_name(state, offset);
    bool directory = entry_is_directory(state, offset);
    if (!directory && is_playlist_name(name)) {
        return note_playlist(state, name);
    }
    bool cue = !directory && scan_is_cue_name(name);
    if (!directory && skipped_file(state, job, offset, cue)) {
        return SCAN_OK;
    }
    if (cue) {
        return scan_add_cue(state, job, stats, offset);
    }
    if (!scan_append_path(state, name, directory)) {
        return SCAN_PATH_TOO_LONG;
    }
    if (!directory) {
        ScanResult result = add_file(state, job, stats);
        scan_truncate_path(state, state->levels[state->depth - 1].path_length);
        return result;
    }
    if (state->depth >= SCAN_MAX_DEPTH) {
        return SCAN_PATH_TOO_LONG;
    }
    Level *child = &state->levels[state->depth];
    child->place.flags = 0;
    if (state->levels[state->depth - 1].placed) {
        uint8_t *place = (uint8_t *)&child->place;
        const uint8_t *listed =
            state->listing + offset + entry_bytes(state, offset, true) - PLACE_BYTES;
        for (uint32_t index = 0; index < PLACE_BYTES; index++) {
            place[index] = listed[index];
        }
    }
    ScanResult result = read_listing(state, &job->files, child, 0);
    uint32_t first = first_placed(state);
    bool unplace = result == SCAN_OK && child->partial && first < state->depth;
    if (unplace) {
        result = unplace_path(state, &job->files, first);
    }
    state->depth += result == SCAN_OK;
    return result == SCAN_NO_MUSIC_ROOT && !unplace ? SCAN_OK : result;
}

static ScanResult walk(ScanState *state, const ScanJob *job, ScanStats *stats)
{
    ScanResult result = read_listing(state, &job->files, &state->levels[0], 0);
    state->depth = result == SCAN_OK ? 1 : 0;
    while (result == SCAN_OK && state->depth > 0) {
        Level *level = &state->levels[state->depth - 1];
        if (level->next >= level->count && level->partial && level->count > 0) {
            scan_truncate_path(state, level->path_length);
            result = read_next_batch(state, &job->files, level);
            continue;
        }
        if (level->next >= level->count) {
            state->listing_used = level->start;
            state->depth--;
            scan_truncate_path(state,
                               state->depth > 0 ? state->levels[state->depth - 1].path_length : 1);
            continue;
        }
        const uint32_t *offsets = (const uint32_t *)(const void *)(state->listing + level->first);
        result = visit_entry(state, job, stats, offsets[level->next++]);
    }
    return result;
}

static bool split_memory(ScanState **state, const ScanJob *job)
{
    uintptr_t start = (uintptr_t)job->memory;
    uint32_t skip = (uint32_t)((ALIGNMENT - start % ALIGNMENT) % ALIGNMENT);
    uint32_t header = (uint32_t)((sizeof(ScanState) + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1));
    uintptr_t own = (uintptr_t)job->listing;
    uint32_t own_skip =
        (uint32_t)((LISTING_ALIGNMENT - own % LISTING_ALIGNMENT) % LISTING_ALIGNMENT);
    bool separate = job->listing != NULL && job->listing_size >= MINIMUM_LISTING + own_skip;
    if (job->size < skip + header + MINIMUM_LISTING * (separate ? 1u : 2u)) {
        return false;
    }
    uint32_t size = job->size - skip;
    uint32_t listing = separate ? job->listing_size - own_skip : size / LISTING_SHARE;
    listing = listing < MINIMUM_LISTING ? MINIMUM_LISTING : listing;
    listing = listing > MAXIMUM_LISTING ? MAXIMUM_LISTING : listing;
    uint8_t *memory = (uint8_t *)job->memory + skip;
    uint32_t taken = separate ? 0 : listing;
    *state = (ScanState *)(void *)memory;
    (*state)->listing = separate ? (uint8_t *)job->listing + own_skip : memory + header;
    (*state)->listing_size = listing;
    (*state)->listing_used = 0;
    (*state)->playlist_floor = listing;
    (*state)->playlist_count = 0;
    library_builder_init(&(*state)->builder, memory + header + taken, size - header - taken,
                         job->scratch);
    return true;
}

static uint32_t record_parent(const uint8_t *record)
{
    return (uint32_t)record[2] | (uint32_t)record[3] << 8;
}

static uint32_t stem_end(const uint8_t *record)
{
    uint32_t length = (uint32_t)record[0] | (uint32_t)record[1] << 8;
    const char *path = (const char *)record + 4;
    uint32_t end = length;
    for (uint32_t index = record_parent(record) + 1; index < length; index++) {
        end = path[index] == '.' ? index : end;
    }
    return end;
}

static void name_and_folder(ScanState *state, const uint8_t *record, char *folder)
{
    uint32_t parent = record_parent(record);
    const char *path = (const char *)record + 4;
    uint32_t folder_length = parent > 1 ? parent - 2 : 0;
    for (uint32_t index = 0; index < folder_length; index++) {
        folder[index] = path[index + 1];
    }
    folder[folder_length] = '\0';
    uint32_t end = stem_end(record);
    uint32_t count = 0;
    for (uint32_t index = parent; index < end; index++) {
        state->playlist_name[count++] = path[index];
    }
    state->playlist_name[count] = '\0';
}

static ScanResult read_playlist(ScanState *state, const ScanJob *job, const uint8_t *record,
                                bool joined)
{
    ByteSource source;
    uint32_t size = 0;
    const char *path = (const char *)record + 4;
    if (!job->files.open_file(job->files.context, path, &source, &size)) {
        return SCAN_OK;
    }
    name_and_folder(state, record, state->path);
    ScanResult result = scan_builder_result(
        joined
            ? library_builder_continue_playlist(&state->builder, state->path)
            : library_builder_begin_playlist(&state->builder, state->playlist_name, state->path));
    playlist_reader_open(&state->playlist, source, size);
    while (result == SCAN_OK &&
           playlist_reader_next(&state->playlist, state->entry_text, sizeof state->entry_text)) {
        result = scan_builder_result(library_builder_playlist_entry(
            &state->builder, state->entry_text, state->playlist.start_ms));
    }
    job->files.close_file(job->files.context);
    return result;
}

static uint32_t record_length(const ScanState *state, uint32_t offset)
{
    uint32_t length = (uint32_t)state->listing[offset] | (uint32_t)state->listing[offset + 1] << 8;
    return align4(length + 1 + 4);
}

static uint32_t noted_playlist(const ScanState *state, uint32_t index)
{
    uint32_t offset = state->playlist_floor;
    for (uint32_t skip = 0; skip + 1 + index < state->playlist_count; skip++) {
        offset += record_length(state, offset);
    }
    return offset;
}

static bool at_top(const uint8_t *record, bool internal)
{
    const char *path = (const char *)record + 4;
    return record_parent(record) == (internal ? 3u : 1u) &&
           (path[1] == DRIVE_PATH_INTERNAL) == internal;
}

static char fold(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

static bool same_top_name(const uint8_t *card, const uint8_t *internal)
{
    const char *left = (const char *)card + 4 + record_parent(card);
    const char *right = (const char *)internal + 4 + record_parent(internal);
    uint32_t length = stem_end(card) - record_parent(card);
    if (length != stem_end(internal) - record_parent(internal)) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        if (fold(left[index]) != fold(right[index])) {
            return false;
        }
    }
    return true;
}

static bool has_card_partner(const ScanState *state, const uint8_t *record)
{
    uint32_t offset = state->playlist_floor;
    for (uint32_t index = 0; index < state->playlist_count; index++) {
        const uint8_t *other = state->listing + offset;
        if (at_top(other, false) && same_top_name(other, record)) {
            return true;
        }
        offset += record_length(state, offset);
    }
    return false;
}

static ScanResult read_partners(ScanState *state, const ScanJob *job, const uint8_t *card)
{
    ScanResult result = SCAN_OK;
    uint32_t offset = state->playlist_floor;
    for (uint32_t index = 0; result == SCAN_OK && index < state->playlist_count; index++) {
        const uint8_t *other = state->listing + offset;
        if (at_top(other, true) && same_top_name(card, other)) {
            result = read_playlist(state, job, other, true);
        }
        offset += record_length(state, offset);
    }
    return result;
}

static bool first_card_top(const ScanState *state, uint32_t index, const uint8_t *record)
{
    for (uint32_t earlier = 0; earlier < index; earlier++) {
        const uint8_t *other = state->listing + noted_playlist(state, earlier);
        if (at_top(other, false) && same_top_name(other, record)) {
            return false;
        }
    }
    return at_top(record, false);
}

static ScanResult read_noted(ScanState *state, const ScanJob *job, uint32_t index)
{
    const uint8_t *record = state->listing + noted_playlist(state, index);
    if (at_top(record, true) && has_card_partner(state, record)) {
        return SCAN_OK;
    }
    ScanResult result = read_playlist(state, job, record, false);
    return result == SCAN_OK && first_card_top(state, index, record)
               ? read_partners(state, job, record)
               : result;
}

static ScanResult read_playlists(ScanState *state, const ScanJob *job)
{
    ScanResult result = scan_builder_result(library_builder_prepare(&state->builder));
    library_builder_expect_playlists(&state->builder, state->playlist_count);
    for (uint32_t index = 0; result == SCAN_OK && index < state->playlist_count; index++) {
        result = read_noted(state, job, index);
    }
    return result;
}

ScanResult scan_add_track(ScanState *state, const BuilderTrack *track)
{
    if (state->filter.hidden != NULL && state->filter.hidden(state->filter.context, track)) {
        state->hidden++;
        return SCAN_OK;
    }
    return scan_builder_result(library_builder_add_track(&state->builder, track));
}

ScanResult library_scan(const ScanJob *job, ScanStats *stats)
{
    *stats = (ScanStats){.files = 0, .tracks = 0, .skipped = 0, .length = 0};
    ScanState *state = NULL;
    if (!split_memory(&state, job)) {
        return SCAN_OUT_OF_MEMORY;
    }
    state->path[0] = '/';
    state->filter = job->filter;
    state->hidden = 0;
    library_builder_watch(&state->builder,
                          (BuilderProgress){job->progress.phase, job->progress.context});
    scan_truncate_path(state, 1);
    state->levels[0].place = (ScanPlace){.flags = 0};
    ScanResult result = walk(state, job, stats);
    stats->tracks -= state->hidden;
    if (result == SCAN_OK) {
        result = read_playlists(state, job);
    }
    if (result != SCAN_OK) {
        return result;
    }
    return scan_builder_result(
        library_builder_finish(&state->builder, job->sink, job->capacity, &stats->length));
}
