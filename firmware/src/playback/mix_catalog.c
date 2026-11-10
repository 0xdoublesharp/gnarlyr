#include "playback/mix_catalog.h"

#include <stddef.h>

#include "playback/mix_identity.h"

enum {
    GENERATION_FIELD = 64,
    COUNT_FIELD = 68,
    BANK_FIELD = 70,
    ACTIVE_FIELD = 71,
    BYTES_FIELD = 72,
    RESERVED_FIELD = 76
};

static const StoreKind KIND = {.magic = {'D', 'S', 'M', 'C'}, .version = 1};

typedef struct Request {
    const StoreFile *file;
    const uint32_t *older_than;
} Request;

static StoreSlot slot_of(const StoreFile *file, uint8_t bank)
{
    return (StoreSlot){.file = file, .kind = &KIND, .offset = (uint32_t)bank * MIX_BANK_BYTES};
}

static bool file_valid(const StoreFile *file)
{
    return file->source.read != NULL && file->size >= MIX_CATALOG_BYTES;
}

static bool name_valid(const char *name)
{
    bool named = false;
    for (int index = 0; index < MIX_NAME_BYTES; index++) {
        uint8_t value = (uint8_t)name[index];
        if (value == 0) {
            return named;
        }
        if (value < 32 || value == 127) {
            return false;
        }
        named |= value != ' ';
    }
    return false;
}

static bool size_valid(const MixEntry *entry)
{
    uint32_t count = entry->count;
    return entry->bytes >= count * MIX_RECORD_MIN && entry->bytes <= count * MIX_RECORD_MAX;
}

static bool entry_valid(const MixEntry *entry)
{
    if (!entry->active) {
        return entry->name[0] == 0 && entry->count == 0 && entry->bytes == 0 &&
               entry->file_bank == 0;
    }
    return name_valid(entry->name) && entry->file_bank <= 1 && size_valid(entry);
}

static void encode_entry(const MixEntry *entry, uint8_t *bytes)
{
    for (int index = 0; index < MIX_ENTRY_BYTES; index++) {
        bytes[index] = 0;
    }
    for (int index = 0; index < MIX_NAME_BYTES && entry->name[index] != 0; index++) {
        bytes[index] = (uint8_t)entry->name[index];
    }
    store_put32(bytes + GENERATION_FIELD, entry->generation);
    store_put16(bytes + COUNT_FIELD, entry->count);
    bytes[BANK_FIELD] = entry->file_bank;
    bytes[ACTIVE_FIELD] = entry->active ? 1 : 0;
    store_put32(bytes + BYTES_FIELD, entry->bytes);
}

static bool decode_entry(const uint8_t *bytes, MixEntry *entry)
{
    *entry = (MixEntry){.generation = store_get32(bytes + GENERATION_FIELD),
                        .bytes = store_get32(bytes + BYTES_FIELD),
                        .count = store_get16(bytes + COUNT_FIELD),
                        .file_bank = bytes[BANK_FIELD],
                        .active = bytes[ACTIVE_FIELD] == 1};
    for (int index = 0; index < MIX_NAME_BYTES; index++) {
        entry->name[index] = (char)bytes[index];
    }
    return bytes[ACTIVE_FIELD] <= 1 && store_get32(bytes + RESERVED_FIELD) == 0 &&
           entry_valid(entry);
}

static uint32_t payload_offset(uint8_t bank, uint32_t position)
{
    return (uint32_t)bank * MIX_BANK_BYTES + STORE_BLOCK_BYTES + position;
}

static StoreLoad read_entry(const StoreFile *file, uint8_t bank, uint16_t slot, MixEntry *entry)
{
    uint8_t bytes[MIX_ENTRY_BYTES];
    uint32_t offset = payload_offset(bank, (uint32_t)slot * MIX_ENTRY_BYTES);
    if (!store_file_read(file, offset, bytes, sizeof bytes)) {
        return STORE_UNREADABLE;
    }
    return decode_entry(bytes, entry) ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad check_hash(const StoreFile *file, uint8_t bank, const StoreHeader *header)
{
    uint8_t block[STORE_BLOCK_BYTES];
    StoreSlot slot = slot_of(file, bank);
    StoreScan scan = store_scan_begin();
    while (scan.position < header->bytes) {
        if (!store_scan_next(&slot, &scan, header->bytes, block)) {
            return STORE_UNREADABLE;
        }
    }
    return scan.hash == header->hash ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad check_bank(void *context, uint8_t bank, const StoreHeader *header)
{
    const Request *request = context;
    if (request->older_than != NULL && !store_newer(*request->older_than, header->generation)) {
        return STORE_EMPTY;
    }
    if (header->count != MIX_CATALOG_SLOTS || header->bytes != MIX_PAYLOAD_BYTES) {
        return STORE_CORRUPT;
    }
    StoreLoad load = check_hash(request->file, bank, header);
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS && load == STORE_READY; slot++) {
        MixEntry entry;
        load = read_entry(request->file, bank, slot, &entry);
    }
    return load;
}

static StoreLoad select_bank(MixCatalog *catalog, const uint32_t *older_than)
{
    Request request = {.file = &catalog->file, .older_than = older_than};
    StorePair pair = {.file = &catalog->file,
                      .kind = &KIND,
                      .bank_bytes = MIX_BANK_BYTES,
                      .check = check_bank,
                      .context = &request};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    if (load == STORE_READY) {
        catalog->bank = choice.bank;
        catalog->generation = choice.header.generation;
        catalog->valid = true;
    }
    return load;
}

StoreLoad mix_catalog_open(MixCatalog *catalog, StoreFile file)
{
    *catalog = (MixCatalog){.file = file};
    StoreLoad load = file_valid(&file) ? select_bank(catalog, NULL) : STORE_UNREADABLE;
    catalog->needs_reload = store_blocking(load) || load == STORE_CORRUPT;
    return load;
}

StoreLoad mix_catalog_previous(MixCatalog *catalog)
{
    if (!catalog->valid || catalog->busy || catalog->needs_reload) {
        return STORE_EMPTY;
    }
    uint32_t current = catalog->generation;
    StoreLoad load = select_bank(catalog, &current);
    catalog->needs_reload = store_blocking(load);
    return load == STORE_READY || catalog->needs_reload ? load : STORE_EMPTY;
}

bool mix_catalog_read(const MixCatalog *catalog, uint16_t slot, MixEntry *entry)
{
    if (!file_valid(&catalog->file) || catalog->needs_reload || slot >= MIX_CATALOG_SLOTS) {
        return false;
    }
    if (!catalog->valid) {
        *entry = (MixEntry){0};
        return true;
    }
    return read_entry(&catalog->file, catalog->bank, slot, entry) == STORE_READY;
}

static StoreLoad load_active(const MixCatalog *catalog, StoreHeader *header)
{
    uint8_t block[STORE_BLOCK_BYTES];
    StoreSlot slot = slot_of(&catalog->file, catalog->bank);
    return store_load(&slot, block, header);
}

static bool active_intact(const MixCatalog *catalog)
{
    StoreHeader header;
    Request request = {.file = &catalog->file};
    return load_active(catalog, &header) == STORE_READY &&
           header.generation == catalog->generation &&
           check_bank(&request, catalog->bank, &header) == STORE_READY;
}

static bool editable(const MixCatalog *catalog, uint32_t revision)
{
    return !catalog->busy && !catalog->needs_reload && revision == catalog->generation &&
           file_valid(&catalog->file) && catalog->file.write != NULL;
}

bool mix_catalog_begin(MixCatalogJob *job, MixCatalog *catalog, uint32_t revision, MixPatch patch)
{
    if (!editable(catalog, revision) || patch.slot >= MIX_CATALOG_SLOTS ||
        !entry_valid(&patch.entry)) {
        return false;
    }
    if (catalog->valid && !active_intact(catalog)) {
        catalog->needs_reload = true;
        return false;
    }
    *job =
        (MixCatalogJob){.catalog = catalog,
                        .patch = patch,
                        .write.header = {.generation = catalog->valid ? catalog->generation + 1 : 1,
                                         .bytes = MIX_PAYLOAD_BYTES,
                                         .count = MIX_CATALOG_SLOTS},
                        .bank = catalog->valid ? (uint8_t)(1 - catalog->bank) : 0};
    store_write_begin(&job->write, catalog->valid ? &catalog->generation : NULL);
    catalog->busy = true;
    return true;
}

static bool fill_entries(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    const MixCatalogJob *job = context;
    const MixCatalog *catalog = job->catalog;
    uint8_t patch[MIX_ENTRY_BYTES];
    uint32_t first = (uint32_t)job->patch.slot * MIX_ENTRY_BYTES;
    for (uint32_t index = 0; index < count; index++) {
        block->bytes[index] = 0;
    }
    if (catalog->valid && !store_file_read(&catalog->file, payload_offset(catalog->bank, position),
                                           block->bytes, count)) {
        return false;
    }
    encode_entry(&job->patch.entry, patch);
    for (uint32_t at = position; at < position + count; at++) {
        if (at >= first && at < first + MIX_ENTRY_BYTES) {
            block->bytes[at - position] = patch[at - first];
        }
    }
    return true;
}

StoreResult mix_catalog_step(MixCatalogJob *job)
{
    if (job->write.result != STORE_WORKING) {
        return job->write.result;
    }
    MixCatalog *catalog = job->catalog;
    StoreSlot slot = slot_of(&catalog->file, job->bank);
    StoreResult result = store_write_step(&job->write, &slot, (StoreFill){fill_entries, job});
    if (result == STORE_WORKING) {
        return result;
    }
    catalog->busy = false;
    catalog->needs_reload |= result == STORE_UNCERTAIN;
    if (result == STORE_DONE) {
        catalog->bank = job->bank;
        catalog->generation = job->write.header.generation;
        catalog->valid = true;
    }
    return result;
}

void mix_catalog_abort(MixCatalogJob *job)
{
    if (job->write.result == STORE_WORKING) {
        store_write_abort(&job->write);
        job->catalog->busy = false;
    }
}
