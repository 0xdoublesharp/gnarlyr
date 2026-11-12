#include <stddef.h>

#include "builder_internal.h"
#include "core/hash.h"
#include "library/sort_key.h"

enum {
    PATH_CAPACITY = 1024,
    MAX_PARTS = 64,
    EMPTY_SLOT = 0
};

typedef struct Span {
    uint32_t start;
    uint32_t length;
} Span;

typedef struct Parts {
    char text[PATH_CAPACITY];
    Span spans[MAX_PARTS];
    uint32_t count;
} Parts;

typedef struct ResolveScratch {
    char entry[PATH_CAPACITY];
    char joined[PATH_CAPACITY];
    char normal[PATH_CAPACITY];
    char folded[PATH_CAPACITY];
    char track[PATH_CAPACITY];
    char name[BUILDER_TEXT_CAPACITY];
    char prefix[PATH_CAPACITY];
    uint32_t prefix_length;
    uint32_t prefix_folder;
    Parts raw;
    Parts wanted;
    Parts candidate;
    Span kept[MAX_PARTS];
} ResolveScratch;

static ResolveScratch *scratch_of(const Prepared *prepared)
{
    return prepared->resolve;
}

static uint32_t slots_for(uint32_t count)
{
    uint32_t result = 16;
    while (result < count + count / 4) {
        result *= 2;
    }
    return result;
}

static uint32_t append_folded(char *out, uint32_t used, const char *text, uint32_t length)
{
    if (used >= PATH_CAPACITY - 1) {
        return used;
    }
    return used + (uint32_t)case_fold_make(text, length, out + used, PATH_CAPACITY - used);
}

static bool fold_prefix(BuilderState *state, Prepared *prepared, uint32_t folder_index)
{
    ResolveScratch *scratch = scratch_of(prepared);
    FolderRow chain[BUILDER_MAX_DEPTH];
    uint32_t depth = 0;
    scratch->prefix_folder = UINT32_MAX;
    for (uint32_t node = folder_index; node != 0 && depth < BUILDER_MAX_DEPTH;) {
        if (!builder_read_folder(&prepared->folders, state, node, &chain[depth])) {
            return false;
        }
        node = chain[depth++].parent;
    }
    uint32_t used = 0;
    while (depth > 0) {
        const FolderRow *folder = &chain[--depth];
        if (!builder_read_text(&prepared->heap, folder->name, folder->length, scratch->name)) {
            return false;
        }
        used = append_folded(scratch->prefix, used, scratch->name, folder->length);
        used = used < PATH_CAPACITY - 1 ? used + 1 : used;
        scratch->prefix[used - 1] = '/';
    }
    scratch->prefix_length = used;
    scratch->prefix_folder = folder_index;
    return true;
}

uint32_t builder_folded_path(BuilderState *state, Prepared *prepared, uint32_t position, char *out)
{
    ResolveScratch *scratch = scratch_of(prepared);
    TrackRow row;
    out[0] = '\0';
    if (!builder_read_row(&prepared->rows, state, prepared->positions[position], &row) ||
        (row.folder != scratch->prefix_folder && !fold_prefix(state, prepared, row.folder))) {
        return 0;
    }
    uint32_t used = scratch->prefix_length;
    for (uint32_t index = 0; index < used; index++) {
        out[index] = scratch->prefix[index];
    }
    out[used] = '\0';
    uint32_t length = row.length[BUILDER_FILE];
    if (!builder_read_text(&prepared->heap, row.text[BUILDER_FILE], length, scratch->name)) {
        return 0;
    }
    return append_folded(out, used, scratch->name, length);
}

static uint32_t folded_name_hash(BuilderState *state, Prepared *prepared, uint32_t position,
                                 char *folded)
{
    char *name = scratch_of(prepared)->name;
    TrackRow row;
    if (!builder_read_row(&prepared->rows, state, prepared->positions[position], &row) ||
        !builder_read_text(&prepared->heap, row.text[BUILDER_FILE], row.length[BUILDER_FILE],
                           name)) {
        return 0;
    }
    uint32_t length =
        (uint32_t)case_fold_make(name, row.length[BUILDER_FILE], folded, PATH_CAPACITY);
    return hash_update(HASH_START, folded, length);
}

static bool same_folded_path(BuilderState *state, Prepared *prepared, uint32_t position,
                             const char *wanted, uint32_t length)
{
    char *folded = scratch_of(prepared)->track;
    uint32_t found = builder_folded_path(state, prepared, position, folded);
    if (found != length) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        if (folded[index] != wanted[index]) {
            return false;
        }
    }
    return true;
}

static uint8_t path_check(uint32_t hash)
{
    return (uint8_t)(hash >> 24);
}

static void index_exact(BuilderState *state, Prepared *prepared, uint32_t position,
                        const char *folded, uint32_t length)
{
    uint32_t hash = hash_update(HASH_START, folded, length);
    for (uint32_t probe = 0;; probe++) {
        uint32_t index = (hash + probe) & prepared->exact_mask;
        uint16_t *slot = &prepared->exact[index];
        if (*slot == EMPTY_SLOT) {
            *slot = (uint16_t)(position + 1);
            prepared->exact_check[index] = path_check(hash);
            return;
        }
        if (prepared->exact_check[index] == path_check(hash) &&
            same_folded_path(state, prepared, *slot - 1u, folded, length)) {
            *slot = *slot > position + 1 ? *slot : (uint16_t)(position + 1);
            return;
        }
    }
}

static uint32_t path_name_hash(BuilderState *state, Prepared *prepared, uint32_t position,
                               const char *folded, uint32_t length)
{
    if (length + 1 >= PATH_CAPACITY) {
        return folded_name_hash(state, prepared, position, scratch_of(prepared)->joined);
    }
    uint32_t start = length;
    while (start > 0 && folded[start - 1] != '/') {
        start--;
    }
    return hash_update(HASH_START, folded + start, length - start);
}

static void index_track(BuilderState *state, Prepared *prepared, uint32_t position)
{
    char *folded = scratch_of(prepared)->folded;
    uint32_t length = builder_folded_path(state, prepared, position, folded);
    index_exact(state, prepared, position, folded, length);
    uint32_t hash = path_name_hash(state, prepared, position, folded, length);
    uint16_t *head = &prepared->by_name[hash & prepared->by_name_mask];
    prepared->next_same_name[position] = *head;
    *head = (uint16_t)(position + 1);
}

static bool take_index(BuilderState *state, Prepared *prepared, uint32_t count, uint32_t slots)
{
    prepared->index_mark = state->arena.used;
    prepared->positions = builder_load_column(state, prepared->final_tracks, count);
    prepared->exact = arena_take(&state->arena, slots * 2);
    prepared->exact_check = arena_take(&state->arena, slots);
    prepared->by_name = arena_take(&state->arena, slots * 2);
    prepared->next_same_name = arena_take(&state->arena, count * 2 + 2);
    prepared->resolve = arena_take(&state->arena, (uint32_t)sizeof(ResolveScratch));
    if (prepared->resolve != NULL) {
        scratch_of(prepared)->prefix_folder = UINT32_MAX;
    }
    prepared->exact_mask = slots - 1;
    prepared->by_name_mask = slots - 1;
    return !state->arena.failed && prepared->positions != NULL;
}

bool builder_index_paths(BuilderState *state, Prepared *prepared)
{
    uint32_t count = state->track_count;
    uint32_t slots = slots_for(count);
    if (!take_index(state, prepared, count, slots)) {
        return false;
    }
    for (uint32_t slot = 0; slot < slots; slot++) {
        prepared->exact[slot] = EMPTY_SLOT;
        prepared->by_name[slot] = EMPTY_SLOT;
    }
    uint32_t mark = state->arena.used;
    uint16_t *final_of = arena_take(&state->arena, count * 2 + 2);
    if (final_of == NULL) {
        return false;
    }
    for (uint32_t position = 0; position < count; position++) {
        final_of[prepared->positions[position]] = (uint16_t)position;
    }
    for (uint32_t row = 0; row < count; row++) {
        index_track(state, prepared, final_of[row]);
    }
    state->arena.used = mark;
    return !state->store.io_failed;
}

static bool contains_url(const char *entry)
{
    for (const char *cursor = entry; cursor[0] != '\0'; cursor++) {
        if (cursor[0] == ':' && cursor[1] == '/' && cursor[2] == '/') {
            return true;
        }
    }
    return false;
}

static void split_parts(const char *path, uint32_t length, Parts *parts)
{
    parts->count = 0;
    uint32_t start = 0;
    for (uint32_t index = 0; index <= length && parts->count < MAX_PARTS; index++) {
        if (index < length && path[index] != '/') {
            continue;
        }
        parts->spans[parts->count] = (Span){.start = start, .length = index - start};
        parts->count++;
        start = index + 1;
    }
}

static bool is_dots(const char *text, Span span, uint32_t dots)
{
    if (span.length != dots) {
        return false;
    }
    for (uint32_t index = 0; index < dots; index++) {
        if (text[span.start + index] != '.') {
            return false;
        }
    }
    return true;
}

static uint32_t leading_slashes(const char *path, uint32_t length)
{
    if (length >= 2 && path[0] == '/' && path[1] == '/' && (length < 3 || path[2] != '/')) {
        return 2;
    }
    return length >= 1 && path[0] == '/' ? 1 : 0;
}

static uint32_t keep_components(const char *path, const Parts *raw, uint32_t initial, Span *kept)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < raw->count; index++) {
        Span span = raw->spans[index];
        if (span.length == 0 || is_dots(path, span, 1)) {
            continue;
        }
        bool parent = is_dots(path, span, 2);
        bool keep_parent =
            (initial == 0 && count == 0) || (count > 0 && is_dots(path, kept[count - 1], 2));
        if (!parent || keep_parent) {
            kept[count++] = span;
        } else if (count > 0) {
            count--;
        }
    }
    return count;
}

static uint32_t copy_span(char *out, uint32_t used, const char *path, Span span)
{
    for (uint32_t byte = 0; byte < span.length && used + 1 < PATH_CAPACITY; byte++) {
        out[used++] = path[span.start + byte];
    }
    return used;
}

static uint32_t normalize(ResolveScratch *scratch, const char *path, uint32_t length)
{
    char *out = scratch->normal;
    split_parts(path, length, &scratch->raw);
    uint32_t initial = leading_slashes(path, length);
    uint32_t count = keep_components(path, &scratch->raw, initial, scratch->kept);
    uint32_t used = 0;
    for (uint32_t index = 0; index < initial; index++) {
        out[used++] = '/';
    }
    for (uint32_t index = 0; index < count; index++) {
        used = copy_span(out, used, path, scratch->kept[index]);
        if (index + 1 < count && used + 1 < PATH_CAPACITY) {
            out[used++] = '/';
        }
    }
    if (used == 0) {
        out[used++] = '.';
    }
    out[used] = '\0';
    return used;
}

static uint32_t find_exact(BuilderState *state, Prepared *prepared, const char *candidate,
                           uint32_t length)
{
    ResolveScratch *scratch = scratch_of(prepared);
    char *folded = scratch->folded;
    uint32_t normal_length = normalize(scratch, candidate, length);
    uint32_t folded_length =
        (uint32_t)case_fold_make(scratch->normal, normal_length, folded, PATH_CAPACITY);
    uint32_t hash = hash_update(HASH_START, folded, folded_length);
    for (uint32_t probe = 0; probe <= prepared->exact_mask; probe++) {
        uint32_t index = (hash + probe) & prepared->exact_mask;
        uint16_t slot = prepared->exact[index];
        if (slot == EMPTY_SLOT) {
            return BUILDER_UNSET;
        }
        if (prepared->exact_check[index] == path_check(hash) &&
            same_folded_path(state, prepared, slot - 1u, folded, folded_length)) {
            return slot - 1u;
        }
    }
    return BUILDER_UNSET;
}

static uint32_t join(const char *folder, const char *path, uint32_t length, char *out)
{
    uint32_t used = 0;
    if (path[0] != '/' && folder[0] != '\0') {
        for (; folder[used] != '\0' && used < PATH_CAPACITY / 2; used++) {
            out[used] = folder[used];
        }
        if (out[used - 1] != '/') {
            out[used++] = '/';
        }
    }
    for (uint32_t index = 0; index < length && used + 1 < PATH_CAPACITY; index++) {
        out[used++] = path[index];
    }
    out[used] = '\0';
    return used;
}

static uint32_t fold_parts(Parts *raw, const char *path, uint32_t length, Parts *parts)
{
    split_parts(path, length, raw);
    uint32_t used = 0;
    parts->count = 0;
    for (uint32_t index = 0; index < raw->count; index++) {
        Span span = raw->spans[index];
        if (span.length == 0 || is_dots(path, span, 1) || is_dots(path, span, 2)) {
            continue;
        }
        uint32_t folded = (uint32_t)case_fold_make(path + span.start, span.length,
                                                   parts->text + used, PATH_CAPACITY - used);
        parts->spans[parts->count++] = (Span){.start = used, .length = folded};
        used += folded < PATH_CAPACITY - used ? folded : 0;
    }
    return parts->count;
}

static bool same_span(const Parts *left, Span a, const Parts *right, Span b)
{
    if (a.length != b.length) {
        return false;
    }
    for (uint32_t index = 0; index < a.length; index++) {
        if (left->text[a.start + index] != right->text[b.start + index]) {
            return false;
        }
    }
    return true;
}

static uint32_t common_suffix(const Parts *left, const Parts *right)
{
    uint32_t length = 0;
    while (length < left->count && length < right->count &&
           same_span(left, left->spans[left->count - 1 - length], right,
                     right->spans[right->count - 1 - length])) {
        length++;
    }
    return length;
}

typedef struct Best {
    uint32_t score;
    uint32_t position;
    uint32_t at_score;
} Best;

static void consider(Best *best, uint32_t score, uint32_t position)
{
    if (best->at_score == 0 || score > best->score) {
        *best = (Best){.score = score, .position = position, .at_score = 1};
    } else if (score == best->score) {
        best->at_score++;
        best->position = position > best->position ? position : best->position;
    }
}

static uint32_t find_by_suffix(BuilderState *state, Prepared *prepared)
{
    ResolveScratch *scratch = scratch_of(prepared);
    const Parts *wanted = &scratch->wanted;
    Span last = wanted->spans[wanted->count - 1];
    uint32_t hash = hash_update(HASH_START, wanted->text + last.start, last.length);
    Best best = {.at_score = 0};
    for (uint16_t link = prepared->by_name[hash & prepared->by_name_mask]; link != EMPTY_SLOT;
         link = prepared->next_same_name[link - 1]) {
        uint32_t position = link - 1u;
        uint32_t length = builder_folded_path(state, prepared, position, scratch->track);
        fold_parts(&scratch->raw, scratch->track, length, &scratch->candidate);
        Parts *candidate = &scratch->candidate;
        if (candidate->count > 0 &&
            same_span(wanted, last, candidate, candidate->spans[candidate->count - 1])) {
            consider(&best, common_suffix(wanted, candidate), position);
        }
    }
    return best.at_score == 1 ? best.position : BUILDER_UNSET;
}

static uint32_t row_start(BuilderState *state, Prepared *prepared, uint32_t position)
{
    TrackRow row;
    if (!builder_read_row(&prepared->rows, state, prepared->positions[position], &row)) {
        return BUILDER_ANY_START;
    }
    return row.start_ms;
}

static uint32_t pick_part(BuilderState *state, Prepared *prepared, uint32_t position,
                          uint32_t start_ms)
{
    ResolveScratch *scratch = scratch_of(prepared);
    uint32_t length = builder_folded_path(state, prepared, position, scratch->normal);
    uint32_t hash = folded_name_hash(state, prepared, position, scratch->folded);
    uint32_t found = BUILDER_UNSET;
    for (uint16_t link = prepared->by_name[hash & prepared->by_name_mask]; link != EMPTY_SLOT;
         link = prepared->next_same_name[link - 1]) {
        if ((found == BUILDER_UNSET || link - 1u > found) &&
            row_start(state, prepared, link - 1u) == start_ms &&
            same_folded_path(state, prepared, link - 1u, scratch->normal, length)) {
            found = link - 1u;
        }
    }
    return found != BUILDER_UNSET ? found : position;
}

static uint32_t resolve_path(BuilderState *state, Prepared *prepared, const char *folder,
                             const char *entry)
{
    ResolveScratch *scratch = scratch_of(prepared);
    if (contains_url(entry)) {
        return BUILDER_UNSET;
    }
    uint32_t length = 0;
    for (; entry[length] != '\0' && length + 1 < PATH_CAPACITY; length++) {
        scratch->entry[length] = entry[length] == '\\' ? '/' : entry[length];
    }
    scratch->entry[length] = '\0';
    uint32_t joined = join(folder, scratch->entry, length, scratch->joined);
    uint32_t found = find_exact(state, prepared, scratch->joined, joined);
    if (found == BUILDER_UNSET) {
        found = find_exact(state, prepared, scratch->entry, length);
    }
    if (found != BUILDER_UNSET) {
        return found;
    }
    uint32_t parts = fold_parts(&scratch->raw, scratch->entry, length, &scratch->wanted);
    return parts > 0 ? find_by_suffix(state, prepared) : BUILDER_UNSET;
}

uint32_t builder_resolve_entry(BuilderState *state, Prepared *prepared, const char *folder,
                               const char *entry, uint32_t start_ms)
{
    uint32_t found = resolve_path(state, prepared, folder, entry);
    if (found == BUILDER_UNSET || start_ms == BUILDER_ANY_START) {
        return found;
    }
    return pick_part(state, prepared, found, start_ms);
}
