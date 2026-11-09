#pragma once

#include "library/library.h"
#include "playback/mix_catalog.h"

typedef struct MixIdentity {
    char path[MIX_PATH_BYTES];
    uint32_t start_ms;
    uint32_t end_ms;
} MixIdentity;

typedef struct MixSource {
    bool (*read)(const void *context, uint16_t index, MixIdentity *identity);
    const void *context;
    uint16_t count;
} MixSource;

typedef struct MixMeasure {
    MixSource source;
    uint32_t bytes;
    uint16_t index;
    MixResult result;
} MixMeasure;

typedef struct MixTape {
    MixIO io;
    uint32_t generation;
    uint32_t bytes;
    uint32_t hash;
    uint16_t count;
    MixLoad load;
} MixTape;

typedef struct MixTapeCursor {
    const MixTape *tape;
    uint32_t offset;
    uint16_t index;
} MixTapeCursor;

typedef struct MixTapeCheck {
    MixTape *tape;
    uint32_t generation;
    uint32_t hash;
    uint32_t position;
    uint16_t index;
    uint8_t stage;
    MixResult result;
} MixTapeCheck;

typedef struct MixTapeWrite {
    MixIO io;
    MixMeasure measured;
    uint32_t generation;
    uint32_t hash;
    uint32_t verify_hash;
    uint32_t position;
    uint16_t index;
    uint16_t record_size;
    uint16_t record_position;
    uint8_t record[MIX_RECORD_MAX];
    uint8_t stage;
    MixResult result;
} MixTapeWrite;

MixLoad mix_identity_read(const Library *library, uint16_t track, MixIdentity *identity);
bool mix_identity_equal(const MixIdentity *a, const MixIdentity *b);
uint32_t mix_identity_hash(const MixIdentity *identity);
void mix_measure_begin(MixMeasure *measure, MixSource source);
MixResult mix_measure_step(MixMeasure *measure);
bool mix_tape_write_begin(MixTapeWrite *job, MixIO io, const MixMeasure *measured,
                          uint32_t generation);
MixResult mix_tape_write_step(MixTapeWrite *job);
void mix_tape_write_abort(MixTapeWrite *job);
void mix_tape_check_begin(MixTapeCheck *job, MixTape *tape, MixIO io, uint32_t generation);
MixResult mix_tape_check_step(MixTapeCheck *job);
void mix_tape_cursor(MixTapeCursor *cursor, const MixTape *tape);
MixLoad mix_tape_next(MixTapeCursor *cursor, MixIdentity *identity);
