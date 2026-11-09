#include "playback/mix_catalog.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    MAGIC = 0x4143584D,
    VERSION = 1,
    HEADER_FIELDS = 24
};
enum {
    INVALIDATE,
    WRITE,
    VERIFY,
    COMMIT
};

typedef struct Bank {
    MixLoad result;
    uint32_t generation;
} Bank;

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

static bool io_valid(const MixIO *io)
{
    return io->source.read != NULL && io->size >= MIX_CATALOG_BYTES;
}

static bool newer(uint32_t a, uint32_t b)
{
    uint32_t distance = a - b;
    return distance != 0 && distance < UINT32_C(0x80000000);
}

static bool entry_valid(const MixEntry *entry)
{
    if (!entry->active) {
        return entry->name[0] == 0 && entry->count == 0 && entry->bytes == 0 &&
               entry->file_bank == 0;
    }
    bool named = false;
    for (int index = 0; index < MIX_NAME_BYTES; index++) {
        uint8_t value = (uint8_t)entry->name[index];
        if (value == 0) {
            return named && entry->file_bank <= 1 &&
                   (entry->count == 0
                        ? entry->bytes == 0
                        : entry->bytes >= (uint32_t)entry->count * 11 &&
                              entry->bytes <= (uint32_t)entry->count * MIX_RECORD_MAX);
        }
        if (value < 32 || value == 127) {
            return false;
        }
        named |= value != ' ';
    }
    return false;
}

static void encode_entry(const MixEntry *entry, uint8_t *bytes)
{
    for (int index = 0; index < MIX_ENTRY_BYTES; index++) {
        bytes[index] = 0;
    }
    for (int index = 0; index < MIX_NAME_BYTES && entry->name[index] != 0; index++) {
        bytes[index] = (uint8_t)entry->name[index];
    }
    put32(bytes + 64, entry->generation);
    put16(bytes + 68, entry->count);
    bytes[70] = entry->file_bank;
    bytes[71] = entry->active ? 1 : 0;
    put32(bytes + 72, entry->bytes);
}

static bool decode_entry(const uint8_t *bytes, MixEntry *entry)
{
    *entry = (MixEntry){.generation = get32(bytes + 64),
                        .count = get16(bytes + 68),
                        .file_bank = bytes[70],
                        .active = bytes[71] == 1,
                        .bytes = get32(bytes + 72)};
    for (int index = 0; index < MIX_NAME_BYTES; index++) {
        entry->name[index] = (char)bytes[index];
    }
    return bytes[71] <= 1 && get32(bytes + 76) == 0 && entry_valid(entry);
}

static uint32_t payload_offset(uint8_t bank, uint32_t position)
{
    return (uint32_t)bank * MIX_BANK_BYTES + MIX_HEADER_BYTES + position;
}

static MixLoad read_entry(const MixIO *io, uint8_t bank, uint16_t slot, MixEntry *entry)
{
    uint8_t bytes[MIX_ENTRY_BYTES];
    if (!io->source.read(io->source.context, payload_offset(bank, (uint32_t)slot * MIX_ENTRY_BYTES),
                         bytes, sizeof bytes)) {
        return MIX_UNREADABLE;
    }
    return decode_entry(bytes, entry) ? MIX_READY : MIX_CORRUPT;
}

static bool header_prefix(const uint8_t *bytes)
{
    return get32(bytes) == MAGIC && get32(bytes + 20) == hash_update(HASH_START, bytes, 20);
}

static bool blank(const uint8_t *bytes)
{
    for (int index = 0; index < MIX_HEADER_BYTES; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static MixLoad check_payload(const MixIO *io, uint8_t bank, uint32_t expected)
{
    uint8_t bytes[512];
    uint32_t hash = HASH_START;
    for (uint32_t position = 0; position < MIX_PAYLOAD_BYTES; position += sizeof bytes) {
        if (!io->source.read(io->source.context, payload_offset(bank, position), bytes,
                             sizeof bytes)) {
            return MIX_UNREADABLE;
        }
        hash = hash_update(hash, bytes, sizeof bytes);
    }
    if (hash != expected) {
        return MIX_CORRUPT;
    }
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixEntry entry;
        MixLoad load = read_entry(io, bank, slot, &entry);
        if (load != MIX_READY) {
            return load;
        }
    }
    return MIX_READY;
}

static Bank read_bank(const MixIO *io, uint8_t bank)
{
    uint8_t bytes[MIX_HEADER_BYTES];
    if (!io->source.read(io->source.context, (uint32_t)bank * MIX_BANK_BYTES, bytes,
                         sizeof bytes)) {
        return (Bank){.result = MIX_UNREADABLE};
    }
    if (!header_prefix(bytes)) {
        return (Bank){.result = blank(bytes) ? MIX_EMPTY : MIX_CORRUPT};
    }
    if (get16(bytes + 4) != VERSION) {
        return (Bank){.result = MIX_UNSUPPORTED};
    }
    if (get16(bytes + 6) != MIX_CATALOG_SLOTS || get32(bytes + 12) != MIX_PAYLOAD_BYTES) {
        return (Bank){.result = MIX_CORRUPT};
    }
    return (Bank){.generation = get32(bytes + 8),
                  .result = check_payload(io, bank, get32(bytes + 16))};
}

static bool unreadable(MixLoad result)
{
    return result == MIX_UNREADABLE || result == MIX_UNSUPPORTED;
}

MixLoad mix_catalog_open(MixCatalog *catalog, MixIO io)
{
    *catalog = (MixCatalog){.io = io};
    if (!io_valid(&io)) {
        catalog->needs_reload = true;
        return MIX_UNREADABLE;
    }
    Bank banks[2] = {read_bank(&io, 0), read_bank(&io, 1)};
    for (int index = 0; index < 2; index++) {
        if (unreadable(banks[index].result)) {
            catalog->needs_reload = true;
            return banks[index].result;
        }
    }
    if (banks[0].result != MIX_READY && banks[1].result != MIX_READY) {
        catalog->needs_reload = banks[0].result == MIX_CORRUPT || banks[1].result == MIX_CORRUPT;
        return catalog->needs_reload ? MIX_CORRUPT : MIX_EMPTY;
    }
    catalog->bank =
        banks[1].result == MIX_READY &&
                (banks[0].result != MIX_READY || newer(banks[1].generation, banks[0].generation))
            ? 1
            : 0;
    catalog->generation = banks[catalog->bank].generation;
    catalog->valid = true;
    return MIX_READY;
}

MixLoad mix_catalog_previous(MixCatalog *catalog)
{
    if (!catalog->valid || catalog->busy || catalog->needs_reload) {
        return MIX_EMPTY;
    }
    uint8_t bank = (uint8_t)(1 - catalog->bank);
    Bank previous = read_bank(&catalog->io, bank);
    if (unreadable(previous.result)) {
        catalog->needs_reload = true;
        return previous.result;
    }
    if (previous.result != MIX_READY || !newer(catalog->generation, previous.generation)) {
        return MIX_EMPTY;
    }
    catalog->bank = bank;
    catalog->generation = previous.generation;
    return MIX_READY;
}

bool mix_catalog_read(const MixCatalog *catalog, uint16_t slot, MixEntry *entry)
{
    if (!io_valid(&catalog->io) || catalog->needs_reload || slot >= MIX_CATALOG_SLOTS) {
        return false;
    }
    if (!catalog->valid) {
        *entry = (MixEntry){0};
        return true;
    }
    return read_entry(&catalog->io, catalog->bank, slot, entry) == MIX_READY;
}

bool mix_catalog_begin(MixCatalogJob *job, MixCatalog *catalog, uint32_t revision, MixPatch patch)
{
    if (catalog->busy || catalog->needs_reload || revision != catalog->generation ||
        !io_valid(&catalog->io) || catalog->io.write == NULL || patch.slot >= MIX_CATALOG_SLOTS ||
        !entry_valid(&patch.entry)) {
        return false;
    }
    if (catalog->valid) {
        Bank active = read_bank(&catalog->io, catalog->bank);
        if (active.result != MIX_READY || active.generation != catalog->generation) {
            catalog->needs_reload = true;
            return false;
        }
    }
    *job = (MixCatalogJob){.catalog = catalog,
                           .patch = patch,
                           .hash = HASH_START,
                           .verify_hash = HASH_START,
                           .result = MIX_WORKING,
                           .bank = catalog->valid ? (uint8_t)(1 - catalog->bank) : 0};
    catalog->busy = true;
    return true;
}

static MixResult finish(MixCatalogJob *job, MixResult result)
{
    job->catalog->busy = false;
    job->catalog->needs_reload |= result == MIX_UNCERTAIN;
    job->result = result;
    return result;
}

static MixResult write_block(MixCatalogJob *job)
{
    uint8_t bytes[512] = {0};
    MixCatalog *catalog = job->catalog;
    if (catalog->valid && !catalog->io.source.read(catalog->io.source.context,
                                                   payload_offset(catalog->bank, job->position),
                                                   bytes, sizeof bytes)) {
        return finish(job, MIX_FAILED);
    }
    uint8_t patch[MIX_ENTRY_BYTES];
    encode_entry(&job->patch.entry, patch);
    uint32_t first = (uint32_t)job->patch.slot * MIX_ENTRY_BYTES;
    for (uint32_t index = 0; index < sizeof bytes; index++) {
        uint32_t position = job->position + index;
        if (position >= first && position < first + MIX_ENTRY_BYTES) {
            bytes[index] = patch[position - first];
        }
    }
    if (!catalog->io.write(catalog->io.source.context, payload_offset(job->bank, job->position),
                           bytes, sizeof bytes)) {
        return finish(job, MIX_FAILED);
    }
    job->hash = hash_update(job->hash, bytes, sizeof bytes);
    job->position = (uint16_t)(job->position + sizeof bytes);
    if (job->position == MIX_PAYLOAD_BYTES) {
        job->stage = VERIFY;
        job->position = 0;
    }
    return MIX_WORKING;
}

static MixResult verify_block(MixCatalogJob *job)
{
    uint8_t bytes[512];
    MixIO *io = &job->catalog->io;
    if (!io->source.read(io->source.context, payload_offset(job->bank, job->position), bytes,
                         sizeof bytes)) {
        return finish(job, MIX_FAILED);
    }
    job->verify_hash = hash_update(job->verify_hash, bytes, sizeof bytes);
    job->position = (uint16_t)(job->position + sizeof bytes);
    if (job->position == MIX_PAYLOAD_BYTES) {
        job->stage = COMMIT;
        if (job->hash != job->verify_hash) {
            return finish(job, MIX_FAILED);
        }
    }
    return MIX_WORKING;
}

static MixResult commit(MixCatalogJob *job)
{
    uint8_t bytes[MIX_HEADER_BYTES] = {0};
    uint8_t readback[MIX_HEADER_BYTES];
    MixCatalog *catalog = job->catalog;
    uint32_t generation = catalog->valid ? catalog->generation + 1 : 1;
    put32(bytes, MAGIC);
    put16(bytes + 4, VERSION);
    put16(bytes + 6, MIX_CATALOG_SLOTS);
    put32(bytes + 8, generation);
    put32(bytes + 12, MIX_PAYLOAD_BYTES);
    put32(bytes + 16, job->hash);
    put32(bytes + 20, hash_update(HASH_START, bytes, 20));
    uint32_t offset = (uint32_t)job->bank * MIX_BANK_BYTES;
    (void)catalog->io.write(catalog->io.source.context, offset, bytes, sizeof bytes);
    if (!catalog->io.source.read(catalog->io.source.context, offset, readback, sizeof readback)) {
        return finish(job, MIX_UNCERTAIN);
    }
    for (int index = 0; index < HEADER_FIELDS; index++) {
        if (bytes[index] != readback[index]) {
            bool uncertain =
                header_prefix(readback) && newer(get32(readback + 8), catalog->generation);
            return finish(job, uncertain ? MIX_UNCERTAIN : MIX_FAILED);
        }
    }
    catalog->bank = job->bank;
    catalog->generation = generation;
    catalog->valid = true;
    return finish(job, MIX_DONE);
}

MixResult mix_catalog_step(MixCatalogJob *job)
{
    if (job->result != MIX_WORKING) {
        return job->result;
    }
    switch (job->stage) {
    case INVALIDATE: {
        uint8_t bytes[MIX_HEADER_BYTES] = {0};
        MixIO *io = &job->catalog->io;
        if (!io->write(io->source.context, (uint32_t)job->bank * MIX_BANK_BYTES, bytes,
                       sizeof bytes)) {
            return finish(job, MIX_FAILED);
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
    return finish(job, MIX_FAILED);
}

void mix_catalog_abort(MixCatalogJob *job)
{
    if (job->result == MIX_WORKING) {
        (void)finish(job, MIX_FAILED);
    }
}
