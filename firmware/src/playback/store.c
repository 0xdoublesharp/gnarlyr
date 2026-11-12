#include "playback/store.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    GENERATION_FIELD = 8,
    BYTES_FIELD = 12,
    HASH_FIELD = 16,
    BINDING_FIELD = 20,
    CHECK_FIELD = 24
};

enum {
    PROGRESS_EXACT_BYTES = 0x1000000
};

enum {
    STAGE_INVALIDATE,
    STAGE_WRITE,
    STAGE_VERIFY,
    STAGE_COMMIT
};

uint16_t store_get16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

uint32_t store_get32(const uint8_t *bytes)
{
    return (uint32_t)store_get16(bytes) | (uint32_t)store_get16(bytes + 2) << 16;
}

void store_put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void store_put32(uint8_t *bytes, uint32_t value)
{
    store_put16(bytes, (uint16_t)value);
    store_put16(bytes + 2, (uint16_t)(value >> 16));
}

bool store_newer(uint32_t candidate, uint32_t previous)
{
    uint32_t distance = candidate - previous;
    return distance != 0 && distance < UINT32_C(0x80000000);
}

bool store_blocking(StoreLoad load)
{
    return load == STORE_UNREADABLE || load == STORE_UNSUPPORTED;
}

static bool inside(const StoreFile *file, uint32_t offset, uint32_t length)
{
    return offset <= file->size && length <= file->size - offset;
}

bool store_file_read(const StoreFile *file, uint32_t offset, void *bytes, uint32_t length)
{
    return file->source.read != NULL && inside(file, offset, length) &&
           file->source.read(file->source.context, offset, bytes, length);
}

bool store_file_write(const StoreFile *file, uint32_t offset, const void *bytes, uint32_t length)
{
    return file->write != NULL && inside(file, offset, length) &&
           file->write(file->source.context, offset, bytes, length);
}

static uint32_t inline_hash(const uint8_t *block, uint32_t bytes)
{
    return hash_update(HASH_START, block + STORE_FIELD_BYTES, bytes);
}

static bool inline_fits(const StoreKind *kind, uint32_t bytes)
{
    return kind->inline_payload && bytes <= STORE_INLINE_BYTES;
}

void store_encode(const StoreKind *kind, const StoreHeader *header, uint8_t *block)
{
    for (int index = 0; index < STORE_MAGIC_BYTES; index++) {
        block[index] = kind->magic[index];
    }
    bool with_payload = inline_fits(kind, header->bytes);
    store_put16(block + VERSION_FIELD, kind->version);
    store_put16(block + COUNT_FIELD, header->count);
    store_put32(block + GENERATION_FIELD, header->generation);
    store_put32(block + BYTES_FIELD, header->bytes);
    store_put32(block + HASH_FIELD,
                with_payload ? inline_hash(block, header->bytes) : header->hash);
    store_put32(block + BINDING_FIELD, header->binding);
    store_put32(block + CHECK_FIELD, hash_update(HASH_START, block, CHECK_FIELD));
}

static bool blank(const uint8_t *block)
{
    for (int index = 0; index < STORE_BLOCK_BYTES; index++) {
        if (block[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool sealed(const StoreKind *kind, const uint8_t *block)
{
    for (int index = 0; index < STORE_MAGIC_BYTES; index++) {
        if (block[index] != kind->magic[index]) {
            return false;
        }
    }
    return store_get32(block + CHECK_FIELD) == hash_update(HASH_START, block, CHECK_FIELD);
}

StoreLoad store_decode(const StoreKind *kind, const uint8_t *block, StoreHeader *header)
{
    *header = (StoreHeader){0};
    if (!sealed(kind, block)) {
        return blank(block) ? STORE_EMPTY : STORE_CORRUPT;
    }
    if (store_get16(block + VERSION_FIELD) != kind->version) {
        return STORE_UNSUPPORTED;
    }
    header->generation = store_get32(block + GENERATION_FIELD);
    header->bytes = store_get32(block + BYTES_FIELD);
    header->hash = store_get32(block + HASH_FIELD);
    header->binding = store_get32(block + BINDING_FIELD);
    header->count = store_get16(block + COUNT_FIELD);
    bool whole = !kind->inline_payload || (inline_fits(kind, header->bytes) &&
                                           inline_hash(block, header->bytes) == header->hash);
    return whole ? STORE_READY : STORE_CORRUPT;
}

StoreLoad store_load(const StoreSlot *slot, uint8_t *block, StoreHeader *header)
{
    if (!store_file_read(slot->file, slot->offset, block, STORE_BLOCK_BYTES)) {
        *header = (StoreHeader){0};
        return STORE_UNREADABLE;
    }
    return store_decode(slot->kind, block, header);
}

static bool same_header(const StoreHeader *first, const StoreHeader *second)
{
    return first->generation == second->generation && first->bytes == second->bytes &&
           first->hash == second->hash && first->binding == second->binding &&
           first->count == second->count;
}

StoreResult store_publish(const StoreSlot *slot, uint8_t *block, const uint32_t *active)
{
    StoreHeader wanted;
    StoreHeader found;
    if (store_decode(slot->kind, block, &wanted) != STORE_READY) {
        return STORE_FAILED;
    }
    (void)store_file_write(slot->file, slot->offset, block, STORE_BLOCK_BYTES);
    StoreLoad load = store_load(slot, block, &found);
    if (load == STORE_EMPTY || load == STORE_CORRUPT) {
        return STORE_FAILED;
    }
    if (load == STORE_READY && same_header(&wanted, &found)) {
        return STORE_DONE;
    }
    bool older = load == STORE_READY && active != NULL && !store_newer(found.generation, *active);
    return older ? STORE_FAILED : STORE_UNCERTAIN;
}

StoreScan store_scan_begin(void)
{
    return (StoreScan){.position = 0, .hash = HASH_START};
}

uint32_t store_scan_chunk(const StoreScan *scan, uint32_t bytes)
{
    uint32_t left = bytes - scan->position;
    return left < STORE_BLOCK_BYTES ? left : STORE_BLOCK_BYTES;
}

static uint32_t payload_offset(const StoreSlot *slot, uint32_t position)
{
    return slot->offset + STORE_BLOCK_BYTES + position;
}

bool store_scan_next(const StoreSlot *slot, StoreScan *scan, uint32_t bytes, uint8_t *block)
{
    uint32_t count = store_scan_chunk(scan, bytes);
    if (!store_file_read(slot->file, payload_offset(slot, scan->position), block, count)) {
        return false;
    }
    scan->hash = hash_update(scan->hash, block, count);
    scan->position += count;
    return true;
}

void store_write_begin(StoreWrite *job, const uint32_t *active)
{
    job->written = store_scan_begin();
    job->verified = store_scan_begin();
    job->active = active != NULL ? *active : 0;
    job->has_active = active != NULL;
    job->stage = STAGE_INVALIDATE;
    job->result = STORE_WORKING;
}

static StoreResult finish(StoreWrite *job, StoreResult result)
{
    job->result = result;
    return result;
}

static StoreResult invalidate(StoreWrite *job, const StoreSlot *slot)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    if (!store_file_write(slot->file, slot->offset, block, sizeof block)) {
        return finish(job, STORE_FAILED);
    }
    job->stage = job->header.bytes == 0 ? STAGE_COMMIT : STAGE_WRITE;
    return STORE_WORKING;
}

static StoreResult write_block(StoreWrite *job, const StoreSlot *slot, StoreFill fill)
{
    StoreBlock block;
    uint32_t position = job->written.position;
    uint32_t count = store_scan_chunk(&job->written, job->header.bytes);
    if (fill.fill == NULL || !fill.fill(fill.context, position, &block, count) ||
        !store_file_write(slot->file, payload_offset(slot, position), block.bytes, count)) {
        return finish(job, STORE_FAILED);
    }
    job->written.hash = hash_update(job->written.hash, block.bytes, count);
    job->written.position += count;
    if (job->written.position == job->header.bytes) {
        job->stage = STAGE_VERIFY;
    }
    return STORE_WORKING;
}

static StoreResult verify_block(StoreWrite *job, const StoreSlot *slot)
{
    uint8_t block[STORE_BLOCK_BYTES];
    if (!store_scan_next(slot, &job->verified, job->header.bytes, block)) {
        return finish(job, STORE_FAILED);
    }
    if (job->verified.position < job->header.bytes) {
        return STORE_WORKING;
    }
    if (job->verified.hash != job->written.hash) {
        return finish(job, STORE_FAILED);
    }
    job->stage = STAGE_COMMIT;
    return STORE_WORKING;
}

static StoreResult commit(StoreWrite *job, const StoreSlot *slot)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    job->header.hash = job->written.hash;
    store_encode(slot->kind, &job->header, block);
    return finish(job, store_publish(slot, block, job->has_active ? &job->active : NULL));
}

StoreResult store_write_step(StoreWrite *job, const StoreSlot *slot, StoreFill fill)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    switch (job->stage) {
    case STAGE_INVALIDATE:
        return invalidate(job, slot);
    case STAGE_WRITE:
        return write_block(job, slot, fill);
    case STAGE_VERIFY:
        return verify_block(job, slot);
    case STAGE_COMMIT:
        return commit(job, slot);
    }
    return finish(job, STORE_FAILED);
}

void store_write_abort(StoreWrite *job)
{
    if (job->result == STORE_WORKING) {
        job->result = STORE_FAILED;
    }
}

uint8_t store_write_progress(const StoreWrite *job)
{
    uint32_t bytes = job->header.bytes;
    if (job->result == STORE_DONE) {
        return 100;
    }
    if (bytes == 0) {
        return 0;
    }
    uint32_t half = job->written.position / 2 + job->verified.position / 2;
    return (uint8_t)(bytes < PROGRESS_EXACT_BYTES ? half * 100 / bytes : half / (bytes / 100));
}

static StoreLoad load_bank(const StorePair *pair, uint8_t bank, StoreHeader *header)
{
    uint8_t block[STORE_BLOCK_BYTES];
    StoreSlot slot = {.file = pair->file, .kind = pair->kind, .offset = bank * pair->bank_bytes};
    return store_load(&slot, block, header);
}

static StoreLoad blocked(const StoreLoad *loads)
{
    if (store_blocking(loads[0])) {
        return loads[0];
    }
    return store_blocking(loads[1]) ? loads[1] : STORE_EMPTY;
}

static uint8_t newest(const StoreLoad *loads, const StoreChoice *banks)
{
    bool second = loads[1] == STORE_READY &&
                  (loads[0] != STORE_READY ||
                   store_newer(banks[1].header.generation, banks[0].header.generation));
    return second ? 1 : 0;
}

static StoreLoad checked(const StorePair *pair, StoreLoad load, const StoreChoice *bank)
{
    if (load != STORE_READY || pair->check == NULL) {
        return load;
    }
    return pair->check(pair->context, bank->bank, &bank->header);
}

StoreLoad store_pair_open(const StorePair *pair, StoreChoice *choice)
{
    StoreChoice banks[2] = {{.bank = 0}, {.bank = 1}};
    StoreLoad loads[2] = {load_bank(pair, 0, &banks[0].header),
                          load_bank(pair, 1, &banks[1].header)};
    StoreLoad worst = blocked(loads);
    uint8_t bank = newest(loads, banks);
    for (int attempt = 0; attempt < 2 && !store_blocking(worst); attempt++, bank ^= 1) {
        StoreLoad load = checked(pair, loads[bank], &banks[bank]);
        if (load == STORE_READY) {
            *choice = banks[bank];
            return load;
        }
        if (load != STORE_EMPTY) {
            worst = load;
        }
    }
    return worst;
}
