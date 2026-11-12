#include "playback/mix_resolve.h"

#include <stddef.h>

enum {
    STAGE_CLEAR,
    STAGE_INDEX,
    STAGE_RESOLVE
};

enum {
    CLEAR_BATCH = 512,
    WORK_BATCH = 32
};

static bool resolved_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    const MixResolve *job = context;
    if (job->result != STORE_DONE || first > job->count || count > job->count - first ||
        (count > 0 && tracks == NULL)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        tracks[index] = job->tracks[first + index];
    }
    return true;
}

static uint32_t index_capacity(uint32_t tracks)
{
    uint32_t capacity = 1;
    while (capacity < tracks * 2) {
        capacity *= 2;
    }
    return capacity;
}

static bool memory_fits(MixMemory memory, uint32_t entries)
{
    return memory.bytes != NULL && (uintptr_t)memory.bytes % _Alignof(uint16_t) == 0 &&
           memory.size / 2 >= entries;
}

bool mix_resolve_begin(MixResolve *job, const Library *library, const MixTape *tape,
                       MixMemory memory)
{
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    *job = (MixResolve){.library = library, .cursor.tape = tape, .result = STORE_FAILED};
    if (tape->load != STORE_READY || count > UINT16_MAX) {
        return false;
    }
    job->track_limit = (uint16_t)count;
    if (count == 0 || tape->count == 0) {
        job->missing = tape->count;
        job->result = STORE_DONE;
        return true;
    }
    uint32_t capacity = index_capacity(count);
    if (!memory_fits(memory, capacity + tape->count)) {
        return false;
    }
    job->capacity = capacity;
    job->slots = memory.bytes;
    job->tracks = job->slots + capacity;
    job->result = STORE_WORKING;
    return true;
}

static void want(MixResolve *job)
{
    job->hash = mix_identity_hash(&job->wanted);
    job->probe = 0;
    job->matches = 0;
    job->active = true;
}

static bool probe_slot(MixResolve *job, uint16_t *track)
{
    if (job->probe == job->capacity) {
        job->result = STORE_FAILED;
        return false;
    }
    *track = job->slots[(job->hash + job->probe++) & (job->capacity - 1)];
    return true;
}

static bool same_candidate(MixResolve *job, uint16_t track)
{
    MixIdentity identity;
    if (mix_identity_read(job->library, track, &identity) != STORE_READY) {
        job->result = STORE_FAILED;
        return false;
    }
    return mix_identity_equal(&job->wanted, &identity);
}

static void want_track(MixResolve *job)
{
    if (library_hidden(job->library, job->position)) {
        job->position++;
        return;
    }
    StoreLoad load = mix_identity_read(job->library, (uint16_t)job->position, &job->wanted);
    if (load == STORE_READY) {
        want(job);
    } else if (load == STORE_UNREADABLE) {
        job->result = STORE_FAILED;
    } else {
        job->position++;
    }
}

static void index_track(MixResolve *job)
{
    uint16_t track = 0;
    if (!probe_slot(job, &track)) {
        return;
    }
    if (track == LIBRARY_NONE) {
        job->slots[(job->hash + job->probe - 1) & (job->capacity - 1)] = (uint16_t)job->position;
    } else if (!same_candidate(job, track) || ++job->matches < 2) {
        return;
    }
    job->position++;
    job->active = false;
}

static StoreResult build_index(MixResolve *job)
{
    for (int work = 0; work < WORK_BATCH && job->position < job->track_limit; work++) {
        if (job->active) {
            index_track(job);
        } else {
            want_track(job);
        }
        if (job->result != STORE_WORKING) {
            return job->result;
        }
    }
    if (job->position == job->track_limit) {
        job->position = 0;
        job->stage = STAGE_RESOLVE;
        job->active = false;
    }
    return job->result;
}

static void want_saved(MixResolve *job)
{
    StoreLoad load = mix_tape_next(&job->cursor, &job->wanted);
    if (load == STORE_READY) {
        want(job);
    } else {
        job->result = load == STORE_EMPTY ? STORE_DONE : STORE_FAILED;
    }
}

static void finish_saved(MixResolve *job)
{
    if (job->matches == 1) {
        job->tracks[job->count++] = job->found;
    } else {
        job->missing++;
        job->ambiguous = (uint16_t)(job->ambiguous + (job->matches > 1 ? 1u : 0u));
    }
    job->active = false;
}

static void resolve_saved(MixResolve *job)
{
    uint16_t track = 0;
    if (!probe_slot(job, &track)) {
        return;
    }
    if (track == LIBRARY_NONE) {
        finish_saved(job);
    } else if (same_candidate(job, track)) {
        job->found = track;
        if (++job->matches == 2) {
            finish_saved(job);
        }
    }
}

static StoreResult resolve_tape(MixResolve *job)
{
    for (int work = 0; work < WORK_BATCH && job->result == STORE_WORKING; work++) {
        if (job->active) {
            resolve_saved(job);
        } else {
            want_saved(job);
        }
    }
    return job->result;
}

static StoreResult clear_index(MixResolve *job)
{
    for (int step = 0; step < CLEAR_BATCH && job->position < job->capacity; step++) {
        job->slots[job->position++] = LIBRARY_NONE;
    }
    if (job->position == job->capacity) {
        job->position = 0;
        job->stage = STAGE_INDEX;
    }
    return job->result;
}

StoreResult mix_resolve_step(MixResolve *job)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    switch (job->stage) {
    case STAGE_CLEAR:
        return clear_index(job);
    case STAGE_INDEX:
        return build_index(job);
    default:
        return resolve_tape(job);
    }
}

TrackOrderSource mix_resolve_source(MixResolve *job)
{
    return (TrackOrderSource){
        .read = resolved_read, .context = job, .count = job->result == STORE_DONE ? job->count : 0};
}
