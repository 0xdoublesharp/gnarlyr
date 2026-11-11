#include "playback/track_keys.h"

#include <stddef.h>

#include "library/library_content.h"
#include "playback/mix_identity.h"

enum {
    STAGE_KEYS,
    STAGE_WRITE,
    TRACKS_PER_STEP = 8,
    HALF_PERCENT = 50,
    FULL_PERCENT = 100,
    ENTRY_TRACK = 4,
    ENTRY_ARRIVAL = 6
};

_Static_assert(sizeof(TrackKey) == TRACK_KEYS_ENTRY_BYTES, "entries are decoded in place");

const char TRACK_KEYS_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/track_keys.bin";

static const StoreKind KIND = {.magic = {'D', 'S', 'T', 'K'}, .version = 1};

static uint32_t smaller(uint32_t first, uint32_t second)
{
    return first < second ? first : second;
}

static uint32_t bank_offset(uint8_t bank)
{
    return (uint32_t)bank * TRACK_KEYS_BANK_BYTES;
}

static uint32_t payload_offset(uint8_t bank, uint32_t position)
{
    return bank_offset(bank) + STORE_BLOCK_BYTES + position * TRACK_KEYS_ENTRY_BYTES;
}

static void put_entry(uint8_t *bytes, TrackKey entry)
{
    store_put32(bytes, entry.key);
    store_put16(bytes + ENTRY_TRACK, entry.track);
    store_put16(bytes + ENTRY_ARRIVAL, entry.arrival);
}

static void encode(TrackKey *entries, uint32_t count)
{
    uint8_t *bytes = (uint8_t *)entries;
    for (uint32_t index = 0; index < count; index++) {
        put_entry(bytes + index * TRACK_KEYS_ENTRY_BYTES, entries[index]);
    }
}

static void decode(TrackKey *entries, uint32_t count)
{
    const uint8_t *bytes = (const uint8_t *)entries;
    for (uint32_t index = 0; index < count; index++) {
        const uint8_t *entry = bytes + index * TRACK_KEYS_ENTRY_BYTES;
        TrackKey decoded = {store_get32(entry), store_get16(entry + ENTRY_TRACK),
                            store_get16(entry + ENTRY_ARRIVAL)};
        entries[index] = decoded;
    }
}

static bool read_entries(const StoreFile *file, uint32_t offset, TrackKey *entries, uint32_t count)
{
    if (count != 0 &&
        !store_file_read(file, offset, entries, count * (uint32_t)TRACK_KEYS_ENTRY_BYTES)) {
        return false;
    }
    decode(entries, count);
    return true;
}

static StoreLoad check_bank(void *context, uint8_t bank, const StoreHeader *header)
{
    (void)context;
    (void)bank;
    bool whole = header->bytes % TRACK_KEYS_ENTRY_BYTES == 0 &&
                 header->bytes / TRACK_KEYS_ENTRY_BYTES < TRACK_KEYS_CAPACITY;
    return whole ? STORE_READY : STORE_CORRUPT;
}

static void adopt_choice(TrackKeys *keys, const StoreChoice *choice)
{
    keys->generation = choice->header.generation;
    keys->binding = choice->header.binding;
    keys->count = choice->header.bytes / TRACK_KEYS_ENTRY_BYTES;
    keys->latest = choice->header.count;
    keys->bank = choice->bank;
    keys->valid = true;
}

StoreLoad track_keys_open(TrackKeys *keys, StoreFile file)
{
    *keys = (TrackKeys){.file = file};
    if (file.source.read == NULL || file.size < TRACK_KEYS_FILE_BYTES) {
        keys->needs_reload = true;
        return STORE_UNREADABLE;
    }
    StorePair pair = {.file = &keys->file,
                      .kind = &KIND,
                      .bank_bytes = TRACK_KEYS_BANK_BYTES,
                      .check = check_bank};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    keys->needs_reload = store_blocking(load);
    if (load == STORE_READY) {
        adopt_choice(keys, &choice);
    }
    return load;
}

bool track_keys_current(const TrackKeys *keys, const Library *library)
{
    uint32_t fingerprint = 0;
    return keys->valid && !keys->needs_reload &&
           library_content_fingerprint(library, &fingerprint) && keys->binding == fingerprint &&
           keys->count <= library_music_count(library, LIBRARY_TRACKS);
}

bool track_keys_read(const TrackKeys *keys, uint32_t first, uint32_t count, TrackKey *entries)
{
    return keys->valid && first <= keys->count && count <= keys->count - first &&
           read_entries(&keys->file, payload_offset(keys->bank, first), entries, count);
}

static bool before(TrackKey first, TrackKey second)
{
    return first.key != second.key ? first.key < second.key : first.track < second.track;
}

static void swap_entries(TrackKey *entries, uint32_t first, uint32_t second)
{
    TrackKey held = entries[first];
    entries[first] = entries[second];
    entries[second] = held;
}

static void sift(TrackKey *entries, uint32_t root, uint32_t count)
{
    for (uint32_t child = 2 * root + 1; child < count; child = 2 * root + 1) {
        if (child + 1 < count && before(entries[child], entries[child + 1])) {
            child++;
        }
        if (!before(entries[root], entries[child])) {
            return;
        }
        swap_entries(entries, root, child);
        root = child;
    }
}

static void sort_entries(TrackKey *entries, uint32_t count)
{
    for (uint32_t root = count / 2; root-- > 0;) {
        sift(entries, root, count);
    }
    for (uint32_t end = count; end-- > 1;) {
        swap_entries(entries, 0, end);
        sift(entries, 0, end);
    }
}

bool track_keys_update_begin(TrackKeysUpdate *job, TrackKeys *keys, const Library *library,
                             TrackKeysWork work)
{
    uint32_t binding = 0;
    uint32_t tracks = library_music_count(library, LIBRARY_TRACKS);
    uint32_t runs = work.capacity == 0 ? 0 : (tracks + work.capacity - 1) / work.capacity;
    *job = (TrackKeysUpdate){
        .keys = keys, .library = library, .work = work, .tracks = tracks, .result = STORE_FAILED};
    if (keys->needs_reload || keys->file.write == NULL || work.entries == NULL ||
        work.capacity == 0 || runs > TRACK_KEYS_MAX_RUNS || runs > work.capacity ||
        !library_content_fingerprint(library, &binding)) {
        return false;
    }
    uint16_t latest = keys->latest;
    job->binding = binding;
    job->bank = keys->valid ? (uint8_t)(1 - keys->bank) : 0;
    job->arrival = !keys->valid                       ? 0
                   : latest < TRACK_KEYS_LATEST_LIMIT ? (uint16_t)(latest + 1)
                                                      : latest;
    job->result = STORE_WORKING;
    return true;
}

static bool add_track(TrackKeysUpdate *job)
{
    MixIdentity identity;
    uint16_t track = (uint16_t)job->track++;
    StoreLoad load = mix_identity_read(job->library, track, &identity);
    if (load == STORE_CORRUPT) {
        return true;
    }
    if (load != STORE_READY) {
        return false;
    }
    job->work.entries[job->chunk++] = (TrackKey){mix_identity_hash(&identity), track, 0};
    return true;
}

static bool flush_chunk(TrackKeysUpdate *job)
{
    uint32_t count = job->chunk;
    if (count == 0) {
        return true;
    }
    sort_entries(job->work.entries, count);
    encode(job->work.entries, count);
    uint32_t offset = TRACK_KEYS_RUNS_OFFSET + job->written * TRACK_KEYS_ENTRY_BYTES;
    if (!store_file_write(&job->keys->file, offset, job->work.entries,
                          count * (uint32_t)TRACK_KEYS_ENTRY_BYTES)) {
        return false;
    }
    job->runs[job->run_count++] = (TrackKeysRun){.next = job->written, .end = job->written + count};
    job->written += count;
    job->chunk = 0;
    return true;
}

static void start_write(TrackKeysUpdate *job)
{
    const TrackKeys *keys = job->keys;
    uint32_t slice = job->work.capacity / (job->run_count != 0 ? job->run_count : 1u);
    job->slice = (uint16_t)smaller(slice, UINT16_MAX);
    job->write.header = (StoreHeader){.generation = keys->valid ? keys->generation + 1 : 1,
                                      .bytes = job->written * TRACK_KEYS_ENTRY_BYTES,
                                      .binding = job->binding,
                                      .count = keys->latest};
    store_write_begin(&job->write, keys->valid ? &keys->generation : NULL);
    job->stage = STAGE_WRITE;
}

static StoreResult step_keys(TrackKeysUpdate *job)
{
    for (int count = 0; count < TRACKS_PER_STEP && job->track < job->tracks; count++) {
        if (!add_track(job) || (job->chunk == job->work.capacity && !flush_chunk(job))) {
            return STORE_FAILED;
        }
    }
    if (job->track < job->tracks) {
        return STORE_WORKING;
    }
    if (!flush_chunk(job)) {
        return STORE_FAILED;
    }
    start_write(job);
    return STORE_WORKING;
}

static TrackKey *run_slice(const TrackKeysUpdate *job, uint8_t run)
{
    return job->work.entries + (uint32_t)run * job->slice;
}

static bool refill_run(TrackKeysUpdate *job, uint8_t index)
{
    TrackKeysRun *run = &job->runs[index];
    if (run->at < run->held || run->next == run->end) {
        return true;
    }
    uint32_t count = smaller(run->end - run->next, job->slice);
    uint32_t offset = TRACK_KEYS_RUNS_OFFSET + run->next * TRACK_KEYS_ENTRY_BYTES;
    if (!read_entries(&job->keys->file, offset, run_slice(job, index), count)) {
        return false;
    }
    run->next += count;
    run->at = 0;
    run->held = (uint16_t)count;
    return true;
}

static bool lowest_run(TrackKeysUpdate *job, int *best)
{
    *best = -1;
    for (uint8_t index = 0; index < job->run_count; index++) {
        const TrackKeysRun *run = &job->runs[index];
        if (!refill_run(job, index)) {
            return false;
        }
        if (run->at < run->held &&
            (*best < 0 || before(run_slice(job, index)[run->at],
                                 run_slice(job, (uint8_t)*best)[job->runs[*best].at]))) {
            *best = index;
        }
    }
    return *best >= 0;
}

static bool refill_old(TrackKeysUpdate *job)
{
    uint32_t count = smaller(job->keys->count - job->old_next, TRACK_KEYS_BLOCK_ENTRIES);
    if (!track_keys_read(job->keys, job->old_next, count, job->old)) {
        return false;
    }
    job->old_next += count;
    job->old_at = 0;
    job->old_held = (uint16_t)count;
    return true;
}

static bool old_arrival(TrackKeysUpdate *job, TrackKey *entry)
{
    entry->arrival = job->arrival;
    while (job->keys->valid) {
        if (job->old_at == job->old_held) {
            if (job->old_next == job->keys->count) {
                break;
            }
            if (!refill_old(job)) {
                return false;
            }
        }
        TrackKey old = job->old[job->old_at];
        if (old.key == entry->key) {
            entry->arrival = old.arrival;
            return true;
        }
        if (old.key > entry->key) {
            break;
        }
        job->old_at++;
    }
    job->fresh |= job->keys->valid;
    return true;
}

static bool next_entry(TrackKeysUpdate *job, TrackKey *entry)
{
    int best = -1;
    if (!lowest_run(job, &best)) {
        return false;
    }
    TrackKeysRun *run = &job->runs[best];
    *entry = run_slice(job, (uint8_t)best)[run->at++];
    return old_arrival(job, entry);
}

static bool fill(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    TrackKeysUpdate *job = context;
    (void)position;
    for (uint32_t offset = 0; offset < count; offset += TRACK_KEYS_ENTRY_BYTES) {
        TrackKey entry;
        if (!next_entry(job, &entry)) {
            return false;
        }
        put_entry(block->bytes + offset, entry);
    }
    return true;
}

static void adopt_update(TrackKeysUpdate *job)
{
    TrackKeys *keys = job->keys;
    keys->generation = job->write.header.generation;
    keys->binding = job->binding;
    keys->count = job->written;
    keys->latest = job->write.header.count;
    keys->bank = job->bank;
    keys->valid = true;
}

static StoreResult step_write(TrackKeysUpdate *job)
{
    StoreSlot slot = {.file = &job->keys->file, .kind = &KIND, .offset = bank_offset(job->bank)};
    job->write.header.count = job->fresh ? job->arrival : job->keys->latest;
    StoreResult result = store_write_step(&job->write, &slot, (StoreFill){fill, job});
    if (result == STORE_DONE) {
        adopt_update(job);
    }
    job->keys->needs_reload |= result == STORE_UNCERTAIN;
    return result;
}

StoreResult track_keys_update_step(TrackKeysUpdate *job)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    job->result = job->stage == STAGE_KEYS ? step_keys(job) : step_write(job);
    return job->result;
}

void track_keys_update_abort(TrackKeysUpdate *job)
{
    store_write_abort(&job->write);
    if (job->result == STORE_WORKING) {
        job->result = STORE_FAILED;
    }
}

uint8_t track_keys_update_progress(const TrackKeysUpdate *job)
{
    if (job->result == STORE_DONE) {
        return FULL_PERCENT;
    }
    if (job->stage == STAGE_KEYS) {
        return (uint8_t)(job->tracks == 0 ? 0 : job->track * HALF_PERCENT / job->tracks);
    }
    uint32_t written = smaller(store_write_progress(&job->write), FULL_PERCENT - 1);
    return (uint8_t)(HALF_PERCENT + written / 2);
}
