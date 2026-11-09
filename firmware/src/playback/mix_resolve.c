#include "playback/mix_resolve.h"

#include <stddef.h>

static bool resolved_read(const void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    const MixResolve *job = context;
    if (job->result != MIX_DONE || first > job->count || count > job->count - first ||
        (count > 0 && tracks == NULL)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        tracks[index] = job->tracks[first + index];
    }
    return true;
}

bool mix_resolve_begin(MixResolve *job, const Library *library, const MixTape *tape,
                       MixMemory memory)
{
    *job = (MixResolve){.library = library, .cursor.tape = tape, .result = MIX_FAILED};
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    if (tape->load != MIX_READY || count > UINT16_MAX) {
        return false;
    }
    job->track_limit = (uint16_t)count;
    if (count == 0 || tape->count == 0) {
        job->missing = tape->count;
        job->result = MIX_DONE;
        return true;
    }
    uint32_t capacity = 1;
    while (capacity < count * 2) {
        capacity *= 2;
    }
    if (memory.bytes == NULL || (uintptr_t)memory.bytes % _Alignof(uint16_t) != 0 ||
        memory.size < 2u * (capacity + tape->count)) {
        return false;
    }
    job->capacity = capacity;
    job->slots = memory.bytes;
    job->tracks = job->slots + capacity;
    job->result = MIX_WORKING;
    return true;
}

static void finish_current(MixResolve *job)
{
    job->position++;
    job->active = false;
}

static bool wanted_current(MixResolve *job)
{
    MixLoad load = mix_identity_read(job->library, (uint16_t)job->position, &job->wanted);
    if (load == MIX_UNREADABLE) {
        job->result = MIX_FAILED;
        return false;
    }
    if (load != MIX_READY) {
        finish_current(job);
        return false;
    }
    job->hash = mix_identity_hash(&job->wanted);
    job->probe = 0;
    job->matches = 0;
    job->active = true;
    return true;
}

static bool same_candidate(MixResolve *job, uint16_t track)
{
    MixIdentity identity;
    if (mix_identity_read(job->library, track, &identity) != MIX_READY) {
        job->result = MIX_FAILED;
        return false;
    }
    return mix_identity_equal(&job->wanted, &identity);
}

static MixResult build_index(MixResolve *job)
{
    for (int work = 0; work < 32 && job->position < job->track_limit && job->result == MIX_WORKING;
         work++) {
        if (!job->active) {
            (void)wanted_current(job);
            continue;
        }
        if (job->probe == job->capacity) {
            job->result = MIX_FAILED;
            break;
        }
        uint32_t slot = (job->hash + job->probe++) & (job->capacity - 1);
        uint16_t track = job->slots[slot];
        if (track == LIBRARY_NONE) {
            job->slots[slot] = (uint16_t)job->position;
            finish_current(job);
        } else if (same_candidate(job, track) && ++job->matches == 2) {
            finish_current(job);
        }
    }
    if (job->position == job->track_limit) {
        job->position = 0;
        job->stage = 2;
        job->active = false;
    }
    return job->result;
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

static bool wanted_saved(MixResolve *job)
{
    MixLoad load = mix_tape_next(&job->cursor, &job->wanted);
    if (load == MIX_EMPTY) {
        job->result = MIX_DONE;
        return false;
    }
    if (load != MIX_READY) {
        job->result = MIX_FAILED;
        return false;
    }
    job->hash = mix_identity_hash(&job->wanted);
    job->probe = 0;
    job->matches = 0;
    job->active = true;
    return true;
}

static MixResult resolve_saved(MixResolve *job)
{
    for (int work = 0; work < 32 && job->result == MIX_WORKING; work++) {
        if (!job->active) {
            (void)wanted_saved(job);
            continue;
        }
        if (job->probe == job->capacity) {
            job->result = MIX_FAILED;
            break;
        }
        uint32_t slot = (job->hash + job->probe++) & (job->capacity - 1);
        uint16_t track = job->slots[slot];
        if (track == LIBRARY_NONE) {
            finish_saved(job);
        } else if (same_candidate(job, track)) {
            job->found = track;
            if (++job->matches == 2) {
                finish_saved(job);
            }
        }
    }
    return job->result;
}

MixResult mix_resolve_step(MixResolve *job)
{
    if (job->result != MIX_WORKING) {
        return job->result;
    }
    if (job->stage == 0) {
        for (int step = 0; step < 512 && job->position < job->capacity; step++) {
            job->slots[job->position++] = LIBRARY_NONE;
        }
        if (job->position == job->capacity) {
            job->position = 0;
            job->stage = 1;
        }
        return job->result;
    }
    return job->stage == 1 ? build_index(job) : resolve_saved(job);
}

TrackOrderSource mix_resolve_source(const MixResolve *job)
{
    return (TrackOrderSource){
        .read = resolved_read, .context = job, .count = job->result == MIX_DONE ? job->count : 0};
}
