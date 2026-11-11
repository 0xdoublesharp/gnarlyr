#pragma once

#include "playback/mix_identity.h"

enum {
    MOMENTS_CAPACITY = 64,
    MOMENT_LABEL_BYTES = 64,
    MOMENTS_ENTRY_BYTES = MIX_RECORD_MAX + 4 + MOMENT_LABEL_BYTES,
    MOMENTS_BANK_BYTES = STORE_BLOCK_BYTES + MOMENTS_CAPACITY * MOMENTS_ENTRY_BYTES,
    MOMENTS_FILE_BYTES = 2 * MOMENTS_BANK_BYTES
};

typedef struct Moment {
    MixIdentity identity;
    uint32_t position_ms;
    char label[MOMENT_LABEL_BYTES];
} Moment;

typedef struct Moments {
    StoreFile file;
    uint32_t generation;
    uint16_t count;
    uint8_t bank;
    bool valid;
    bool busy;
    bool needs_reload;
} Moments;

typedef struct MomentsJob {
    Moments *moments;
    Moment moment;
    StoreWrite write;
    uint16_t index;
    uint8_t bank;
    bool deleting;
} MomentsJob;

StoreLoad moments_open(Moments *moments, StoreFile file);
bool moments_read(const Moments *moments, uint16_t index, Moment *moment);
bool moments_add(MomentsJob *job, Moments *moments, uint32_t revision, const Moment *moment);
bool moments_delete(MomentsJob *job, Moments *moments, uint32_t revision, uint16_t index);
StoreResult moments_step(MomentsJob *job);
void moments_abort(MomentsJob *job);

typedef struct MomentResolve {
    const Library *library;
    const MixIdentity *identity;
    uint32_t index;
    uint16_t track;
    uint8_t matches;
    StoreResult result;
} MomentResolve;

void moment_resolve_begin(MomentResolve *job, const Library *library, const MixIdentity *identity);
StoreResult moment_resolve_step(MomentResolve *job);
