#include "playback/mix_tape.h"

#include <stddef.h>

#include "core/hash.h"
#include "mix_private.h"

enum {
    MAGIC = 0x5054584D,
    VERSION = 1,
    HEADER_FIELDS = 24
};
enum {
    INVALIDATE,
    WRITE,
    VERIFY,
    COMMIT
};

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16;
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static bool source_valid(MixSource source)
{
    return source.count == 0 || source.read != NULL;
}

static bool bounds(uint16_t count, uint32_t bytes)
{
    return count == 0 ? bytes == 0
                      : bytes >= (uint32_t)count * 11 && bytes <= (uint32_t)count * MIX_RECORD_MAX;
}

static bool header_prefix(const uint8_t *bytes)
{
    return get32(bytes) == MAGIC && get32(bytes + 20) == hash_update(HASH_START, bytes, 20);
}

void mix_measure_begin(MixMeasure *measure, MixSource source)
{
    *measure =
        (MixMeasure){.source = source, .result = source_valid(source) ? MIX_WORKING : MIX_FAILED};
}

MixResult mix_measure_step(MixMeasure *measure)
{
    if (measure->result != MIX_WORKING) {
        return measure->result;
    }
    for (int step = 0; step < 32 && measure->index < measure->source.count; step++) {
        MixIdentity identity;
        uint8_t bytes[MIX_RECORD_MAX];
        if (!measure->source.read(measure->source.context, measure->index, &identity)) {
            measure->result = MIX_FAILED;
            return measure->result;
        }
        uint16_t size = mix_identity_encode(&identity, bytes);
        if (size == 0) {
            measure->result = MIX_FAILED;
            return measure->result;
        }
        measure->bytes += size;
        measure->index++;
    }
    measure->result = measure->index == measure->source.count ? MIX_DONE : MIX_WORKING;
    return measure->result;
}

bool mix_tape_write_begin(MixTapeWrite *job, MixIO io, const MixMeasure *measured,
                          uint32_t generation)
{
    if (measured->result != MIX_DONE || measured->index != measured->source.count ||
        !source_valid(measured->source) || !bounds(measured->source.count, measured->bytes) ||
        io.source.read == NULL || io.write == NULL || io.size < MIX_HEADER_BYTES ||
        measured->bytes > io.size - MIX_HEADER_BYTES) {
        return false;
    }
    *job = (MixTapeWrite){.io = io,
                          .measured = *measured,
                          .generation = generation,
                          .hash = HASH_START,
                          .verify_hash = HASH_START,
                          .result = MIX_WORKING};
    return true;
}

static uint16_t fill_block(MixTapeWrite *job, uint8_t *bytes)
{
    uint16_t count = 0;
    while (count < 512) {
        if (job->record_position == job->record_size) {
            if (job->index == job->measured.source.count) {
                break;
            }
            MixIdentity identity;
            if (!job->measured.source.read(job->measured.source.context, job->index++, &identity) ||
                (job->record_size = mix_identity_encode(&identity, job->record)) == 0) {
                job->result = MIX_FAILED;
                return 0;
            }
            job->record_position = 0;
        }
        bytes[count++] = job->record[job->record_position++];
    }
    return count;
}

static MixResult write_block(MixTapeWrite *job)
{
    uint8_t bytes[512];
    uint16_t count = fill_block(job, bytes);
    if (job->result != MIX_WORKING) {
        return job->result;
    }
    if (count == 0) {
        job->result = job->position == job->measured.bytes ? MIX_WORKING : MIX_FAILED;
        job->stage = VERIFY;
        job->position = 0;
        return job->result;
    }
    if (job->position > job->measured.bytes || count > job->measured.bytes - job->position ||
        !job->io.write(job->io.source.context, MIX_HEADER_BYTES + job->position, bytes, count)) {
        job->result = MIX_FAILED;
        return job->result;
    }
    job->hash = hash_update(job->hash, bytes, count);
    job->position += count;
    return MIX_WORKING;
}

static MixResult verify_block(MixTapeWrite *job)
{
    uint8_t bytes[512];
    uint32_t left = job->measured.bytes - job->position;
    uint32_t count = left < sizeof bytes ? left : sizeof bytes;
    if (count == 0) {
        job->stage = COMMIT;
        job->result = job->hash == job->verify_hash ? MIX_WORKING : MIX_FAILED;
        return job->result;
    }
    if (!job->io.source.read(job->io.source.context, MIX_HEADER_BYTES + job->position, bytes,
                             count)) {
        job->result = MIX_FAILED;
        return job->result;
    }
    job->verify_hash = hash_update(job->verify_hash, bytes, count);
    job->position += count;
    return MIX_WORKING;
}

static MixResult commit(MixTapeWrite *job)
{
    uint8_t bytes[MIX_HEADER_BYTES] = {0};
    uint8_t readback[MIX_HEADER_BYTES];
    put32(bytes, MAGIC);
    put16(bytes + 4, VERSION);
    put16(bytes + 6, job->measured.source.count);
    put32(bytes + 8, job->generation);
    put32(bytes + 12, job->measured.bytes);
    put32(bytes + 16, job->hash);
    put32(bytes + 20, hash_update(HASH_START, bytes, 20));
    (void)job->io.write(job->io.source.context, 0, bytes, sizeof bytes);
    if (!job->io.source.read(job->io.source.context, 0, readback, sizeof readback)) {
        job->result = MIX_UNCERTAIN;
        return job->result;
    }
    for (int index = 0; index < HEADER_FIELDS; index++) {
        if (bytes[index] != readback[index]) {
            job->result = header_prefix(readback) ? MIX_UNCERTAIN : MIX_FAILED;
            return job->result;
        }
    }
    job->result = MIX_DONE;
    return job->result;
}

MixResult mix_tape_write_step(MixTapeWrite *job)
{
    if (job->result != MIX_WORKING) {
        return job->result;
    }
    switch (job->stage) {
    case INVALIDATE: {
        uint8_t bytes[MIX_HEADER_BYTES] = {0};
        if (!job->io.write(job->io.source.context, 0, bytes, sizeof bytes)) {
            job->result = MIX_FAILED;
            return job->result;
        }
        job->stage = WRITE;
        return MIX_WORKING;
    }
    case WRITE:
        return write_block(job);
    case VERIFY:
        return verify_block(job);
    case COMMIT:
        return commit(job);
    }
    job->result = MIX_FAILED;
    return job->result;
}

void mix_tape_write_abort(MixTapeWrite *job)
{
    if (job->result == MIX_WORKING) {
        job->result = MIX_FAILED;
    }
}

static MixLoad read_identity(const MixTape *tape, uint32_t offset, MixIdentity *identity,
                             uint16_t *size)
{
    uint8_t header[10];
    const ByteSource *source = &tape->io.source;
    if (offset > tape->bytes || tape->bytes - offset < sizeof header) {
        return MIX_CORRUPT;
    }
    if (!source->read(source->context, MIX_HEADER_BYTES + offset, header, sizeof header)) {
        return MIX_UNREADABLE;
    }
    uint16_t length = get16(header);
    *identity = (MixIdentity){.start_ms = get32(header + 2), .end_ms = get32(header + 6)};
    if (length == 0 || length >= MIX_PATH_BYTES || length > tape->bytes - offset - sizeof header) {
        return MIX_CORRUPT;
    }
    if (!source->read(source->context, MIX_HEADER_BYTES + offset + sizeof header, identity->path,
                      length)) {
        return MIX_UNREADABLE;
    }
    for (uint16_t index = 0; index < length; index++) {
        if (identity->path[index] == 0) {
            return MIX_CORRUPT;
        }
    }
    *size = (uint16_t)(sizeof header + length);
    return mix_identity_valid(identity) ? MIX_READY : MIX_CORRUPT;
}

static MixLoad read_header(MixTape *tape, uint32_t generation)
{
    uint8_t bytes[MIX_HEADER_BYTES];
    if (!tape->io.source.read(tape->io.source.context, 0, bytes, sizeof bytes)) {
        return MIX_UNREADABLE;
    }
    if (!header_prefix(bytes)) {
        bool empty = true;
        for (int index = 0; index < MIX_HEADER_BYTES; index++) {
            empty &= bytes[index] == 0;
        }
        return empty ? MIX_EMPTY : MIX_CORRUPT;
    }
    if (get16(bytes + 4) != VERSION) {
        return MIX_UNSUPPORTED;
    }
    tape->generation = get32(bytes + 8);
    tape->count = get16(bytes + 6);
    tape->bytes = get32(bytes + 12);
    tape->hash = get32(bytes + 16);
    return tape->generation == generation && bounds(tape->count, tape->bytes) &&
                   tape->bytes <= tape->io.size - MIX_HEADER_BYTES
               ? MIX_READY
               : MIX_CORRUPT;
}

void mix_tape_check_begin(MixTapeCheck *job, MixTape *tape, MixIO io, uint32_t generation)
{
    *tape = (MixTape){.io = io, .load = MIX_UNREADABLE};
    *job = (MixTapeCheck){
        .tape = tape,
        .generation = generation,
        .hash = HASH_START,
        .result = io.source.read != NULL && io.size >= MIX_HEADER_BYTES ? MIX_WORKING : MIX_FAILED};
}

static MixResult check_failed(MixTapeCheck *job, MixLoad load)
{
    job->tape->load = load;
    job->result = MIX_FAILED;
    return job->result;
}

MixResult mix_tape_check_step(MixTapeCheck *job)
{
    if (job->result != MIX_WORKING) {
        return job->result;
    }
    MixTape *tape = job->tape;
    if (job->stage == 0) {
        MixLoad load = read_header(tape, job->generation);
        if (load != MIX_READY) {
            return check_failed(job, load);
        }
        job->stage = 1;
    } else if (job->stage == 1) {
        uint8_t bytes[512];
        uint32_t left = tape->bytes - job->position;
        uint32_t count = left < sizeof bytes ? left : sizeof bytes;
        if (count == 0) {
            if (job->hash != tape->hash) {
                return check_failed(job, MIX_CORRUPT);
            }
            job->position = 0;
            job->stage = 2;
        } else {
            if (!tape->io.source.read(tape->io.source.context, MIX_HEADER_BYTES + job->position,
                                      bytes, count)) {
                return check_failed(job, MIX_UNREADABLE);
            }
            job->hash = hash_update(job->hash, bytes, count);
            job->position += count;
        }
    } else {
        for (int step = 0; step < 32 && job->index < tape->count; step++) {
            MixIdentity identity;
            uint16_t size = 0;
            MixLoad load = read_identity(tape, job->position, &identity, &size);
            if (load != MIX_READY) {
                return check_failed(job, load);
            }
            job->position += size;
            job->index++;
        }
        if (job->index == tape->count) {
            if (job->position != tape->bytes) {
                return check_failed(job, MIX_CORRUPT);
            }
            tape->load = MIX_READY;
            job->result = MIX_DONE;
        }
    }
    return job->result;
}

void mix_tape_cursor(MixTapeCursor *cursor, const MixTape *tape)
{
    *cursor = (MixTapeCursor){.tape = tape};
}

MixLoad mix_tape_next(MixTapeCursor *cursor, MixIdentity *identity)
{
    const MixTape *tape = cursor->tape;
    if (tape->load != MIX_READY) {
        return MIX_CORRUPT;
    }
    if (cursor->index == tape->count) {
        return cursor->offset == tape->bytes ? MIX_EMPTY : MIX_CORRUPT;
    }
    uint16_t size = 0;
    MixLoad load = read_identity(tape, cursor->offset, identity, &size);
    if (load == MIX_READY) {
        cursor->offset += size;
        cursor->index++;
    }
    return load;
}
