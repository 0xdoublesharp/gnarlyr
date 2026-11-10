#include "playback/mix_tape.h"

#include <stddef.h>

enum {
    MEASURE_BATCH = 32,
    SLOT_TENS_FROM_END = 8,
    SLOT_UNITS_FROM_END = 7,
    BANK_FROM_END = 6
};

static const StoreKind KIND = {.magic = {'D', 'S', 'M', 'T'}, .version = 1};

static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/mix00a.bin";

_Static_assert(sizeof PATH <= MIX_TAPE_PATH_BYTES, "a tape path fits its buffer");

bool mix_tape_path(uint16_t slot, uint8_t bank, char *path)
{
    for (size_t index = 0; index < sizeof PATH; index++) {
        path[index] = PATH[index];
    }
    path[sizeof PATH - SLOT_TENS_FROM_END] = (char)('0' + slot / 10 % 10);
    path[sizeof PATH - SLOT_UNITS_FROM_END] = (char)('0' + slot % 10);
    path[sizeof PATH - BANK_FROM_END] = bank == 0 ? 'a' : 'b';
    return slot < MIX_CATALOG_SLOTS && bank < MIX_TAPE_BANKS;
}

static StoreSlot slot_of(const StoreFile *file)
{
    return (StoreSlot){.file = file, .kind = &KIND};
}

static bool source_valid(MixSource source)
{
    return source.count == 0 || source.read != NULL;
}

static bool bounds(uint32_t count, uint32_t bytes)
{
    return bytes >= count * MIX_RECORD_MIN && bytes <= count * MIX_RECORD_MAX;
}

void mix_measure_begin(MixMeasure *measure, MixSource source)
{
    *measure = (MixMeasure){.source = source,
                            .result = source_valid(source) ? STORE_WORKING : STORE_FAILED};
}

static uint16_t measure_one(MixMeasure *measure)
{
    MixIdentity identity;
    if (!measure->source.read(measure->source.context, measure->index, &identity)) {
        return 0;
    }
    return mix_identity_size(&identity);
}

StoreResult mix_measure_step(MixMeasure *measure)
{
    if (measure->result != STORE_WORKING) {
        return measure->result;
    }
    for (int step = 0; step < MEASURE_BATCH && measure->index < measure->source.count; step++) {
        uint16_t size = measure_one(measure);
        if (size == 0) {
            measure->result = STORE_FAILED;
            return measure->result;
        }
        measure->bytes += size;
        measure->index++;
    }
    measure->result = measure->index == measure->source.count ? STORE_DONE : STORE_WORKING;
    return measure->result;
}

static bool measured_fits(const MixMeasure *measured, const StoreFile *file)
{
    return measured->result == STORE_DONE && measured->index == measured->source.count &&
           source_valid(measured->source) && bounds(measured->source.count, measured->bytes) &&
           file->size >= STORE_BLOCK_BYTES && measured->bytes <= file->size - STORE_BLOCK_BYTES;
}

bool mix_tape_write_begin(MixTapeWrite *job, StoreFile file, const MixMeasure *measured,
                          uint32_t generation)
{
    if (!measured_fits(measured, &file) || file.source.read == NULL || file.write == NULL) {
        return false;
    }
    *job = (MixTapeWrite){.file = file,
                          .source = measured->source,
                          .write.header = {.generation = generation,
                                           .bytes = measured->bytes,
                                           .count = measured->source.count}};
    store_write_begin(&job->write, NULL);
    return true;
}

static bool next_record(MixTapeWrite *job)
{
    MixIdentity identity;
    if (job->index == job->source.count ||
        !job->source.read(job->source.context, job->index, &identity)) {
        return false;
    }
    job->index++;
    job->record_size = mix_identity_encode(&identity, job->record);
    job->record_position = 0;
    return job->record_size != 0;
}

static bool fill_records(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    MixTapeWrite *job = context;
    for (uint32_t index = 0; index < count; index++) {
        if (job->record_position == job->record_size && !next_record(job)) {
            return false;
        }
        block->bytes[index] = job->record[job->record_position++];
    }
    bool ended = job->index == job->source.count && job->record_position == job->record_size;
    return position + count < job->write.header.bytes || ended;
}

StoreResult mix_tape_write_step(MixTapeWrite *job)
{
    StoreSlot slot = slot_of(&job->file);
    return store_write_step(&job->write, &slot, (StoreFill){fill_records, job});
}

void mix_tape_write_abort(MixTapeWrite *job)
{
    store_write_abort(&job->write);
}

void mix_tape_check_begin(MixTapeCheck *job, MixTape *tape, StoreFile file, uint32_t generation)
{
    bool usable = file.source.read != NULL && file.size >= STORE_BLOCK_BYTES;
    *tape = (MixTape){.file = file, .load = STORE_UNREADABLE};
    *job = (MixTapeCheck){.tape = tape,
                          .scan = store_scan_begin(),
                          .generation = generation,
                          .result = usable ? STORE_WORKING : STORE_FAILED};
}

static StoreResult check_failed(MixTapeCheck *job, StoreLoad load)
{
    job->tape->load = load;
    job->result = STORE_FAILED;
    return job->result;
}

static StoreResult open_tape(MixTapeCheck *job)
{
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader header;
    MixTape *tape = job->tape;
    StoreSlot slot = slot_of(&tape->file);
    StoreLoad load = store_load(&slot, block, &header);
    if (load != STORE_READY) {
        return check_failed(job, load);
    }
    tape->generation = header.generation;
    tape->bytes = header.bytes;
    tape->hash = header.hash;
    tape->count = header.count;
    bool wanted = header.generation == job->generation && bounds(header.count, header.bytes) &&
                  header.bytes <= tape->file.size - STORE_BLOCK_BYTES;
    job->opened = wanted;
    return wanted ? STORE_WORKING : check_failed(job, STORE_CORRUPT);
}

static bool take_byte(MixTapeCheck *job, uint8_t byte)
{
    MixIdentity identity;
    job->record[job->record_position++] = byte;
    if (job->record_position < MIX_RECORD_FIELDS) {
        return true;
    }
    uint32_t size = (uint32_t)MIX_RECORD_FIELDS + store_get16(job->record);
    if (size < MIX_RECORD_MIN || size > MIX_RECORD_MAX || job->index == job->tape->count) {
        return false;
    }
    if (job->record_position < size) {
        return true;
    }
    job->record_position = 0;
    job->index++;
    return mix_identity_decode(job->record, size, &identity) == size;
}

static bool take_bytes(MixTapeCheck *job, const uint8_t *bytes, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        if (!take_byte(job, bytes[index])) {
            return false;
        }
    }
    return true;
}

static StoreResult finish_check(MixTapeCheck *job)
{
    MixTape *tape = job->tape;
    if (job->scan.hash != tape->hash || job->record_position != 0 || job->index != tape->count) {
        return check_failed(job, STORE_CORRUPT);
    }
    tape->load = STORE_READY;
    job->result = STORE_DONE;
    return job->result;
}

StoreResult mix_tape_check_step(MixTapeCheck *job)
{
    uint8_t block[STORE_BLOCK_BYTES];
    MixTape *tape = job->tape;
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    if (!job->opened) {
        return open_tape(job);
    }
    if (job->scan.position == tape->bytes) {
        return finish_check(job);
    }
    StoreSlot slot = slot_of(&tape->file);
    uint32_t count = store_scan_chunk(&job->scan, tape->bytes);
    if (!store_scan_next(&slot, &job->scan, tape->bytes, block)) {
        return check_failed(job, STORE_UNREADABLE);
    }
    return take_bytes(job, block, count) ? STORE_WORKING : check_failed(job, STORE_CORRUPT);
}

void mix_tape_cursor(MixTapeCursor *cursor, const MixTape *tape)
{
    *cursor = (MixTapeCursor){.tape = tape};
}

StoreLoad mix_tape_next(MixTapeCursor *cursor, MixIdentity *identity)
{
    uint8_t bytes[MIX_RECORD_MAX];
    const MixTape *tape = cursor->tape;
    if (tape->load != STORE_READY || cursor->offset > tape->bytes) {
        return STORE_CORRUPT;
    }
    uint32_t left = tape->bytes - cursor->offset;
    if (cursor->index == tape->count) {
        return left == 0 ? STORE_EMPTY : STORE_CORRUPT;
    }
    uint32_t count = left < sizeof bytes ? left : (uint32_t)sizeof bytes;
    if (!store_file_read(&tape->file, STORE_BLOCK_BYTES + cursor->offset, bytes, count)) {
        return STORE_UNREADABLE;
    }
    uint16_t size = mix_identity_decode(bytes, count, identity);
    cursor->offset += size;
    cursor->index = (uint16_t)(cursor->index + (size != 0 ? 1 : 0));
    return size != 0 ? STORE_READY : STORE_CORRUPT;
}
