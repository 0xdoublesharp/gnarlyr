#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/mix_catalog.h"
#include "playback/mix_identity.h"

enum {
    MIX_TAPE_BANKS = 2,
    MIX_TAPE_PATH_BYTES = 40
};

typedef struct MixSource {
    bool (*read)(void *context, uint16_t index, MixIdentity *identity);
    void *context;
    uint16_t count;
} MixSource;

typedef struct MixMeasure {
    MixSource source;
    uint32_t bytes;
    uint16_t index;
    StoreResult result;
} MixMeasure;

typedef struct MixTape {
    StoreFile file;
    uint32_t generation;
    uint32_t bytes;
    uint32_t hash;
    uint16_t count;
    StoreLoad load;
} MixTape;

typedef struct MixTapeCursor {
    const MixTape *tape;
    uint32_t offset;
    uint16_t index;
} MixTapeCursor;

typedef struct MixTapeCheck {
    MixTape *tape;
    StoreScan scan;
    uint32_t generation;
    uint16_t index;
    uint16_t record_position;
    uint8_t record[MIX_RECORD_MAX];
    bool opened;
    StoreResult result;
} MixTapeCheck;

typedef struct MixTapeWrite {
    StoreFile file;
    MixSource source;
    StoreWrite write;
    uint16_t index;
    uint16_t record_size;
    uint16_t record_position;
    uint8_t record[MIX_RECORD_MAX];
} MixTapeWrite;

bool mix_tape_path(uint16_t slot, uint8_t bank, char *path);
void mix_measure_begin(MixMeasure *measure, MixSource source);
StoreResult mix_measure_step(MixMeasure *measure);
bool mix_tape_write_begin(MixTapeWrite *job, StoreFile file, const MixMeasure *measured,
                          uint32_t generation);
StoreResult mix_tape_write_step(MixTapeWrite *job);
void mix_tape_write_abort(MixTapeWrite *job);
void mix_tape_check_begin(MixTapeCheck *job, MixTape *tape, StoreFile file, uint32_t generation);
StoreResult mix_tape_check_step(MixTapeCheck *job);
void mix_tape_cursor(MixTapeCursor *cursor, const MixTape *tape);
StoreLoad mix_tape_next(MixTapeCursor *cursor, MixIdentity *identity);
