#include "mixtape_store.h"

#include <stddef.h>

#include "core/string_builder.h"

enum {
    MEASURE = 1,
    CHECKPOINT,
    ALLOCATE,
    WRITE_TAPE,
    PUBLISH,
    OPEN,
    RECOVER
};

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const MixtapeFile *source = context;
    HalFile file;
    if (!hal_file_open(&file, source->path)) {
        return false;
    }
    bool read = hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return read;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const MixtapeFile *file = context;
    return hal_file_write_at(file->path, offset, bytes, length);
}

static bool find_file(MixtapeFile *file)
{
    HalFile handle;
    file->size = 0;
    if (!hal_file_open(&handle, file->path)) {
        return false;
    }
    file->size = handle.size;
    hal_file_close(&handle);
    return true;
}

static StoreFile file_io(MixtapeFile *file)
{
    return (StoreFile){.source = {read_file, file}, .write = write_file, .size = file->size};
}

StoreLoad mixtape_store_init(MixtapeStore *store)
{
    *store = (MixtapeStore){.catalog_file.path = "/" FIRMWARE_DATA_DIRECTORY "/mixes.bin"};
    store->load = find_file(&store->catalog_file)
                      ? mix_catalog_open(&store->catalog, file_io(&store->catalog_file))
                      : STORE_EMPTY;
    return store->load;
}

bool mixtape_store_read(const MixtapeStore *store, uint16_t slot, MixEntry *entry)
{
    if (slot >= MIX_CATALOG_SLOTS || (store->load != STORE_EMPTY && store->load != STORE_READY)) {
        return false;
    }
    if (store->load == STORE_EMPTY) {
        *entry = (MixEntry){0};
        return true;
    }
    return mix_catalog_read(&store->catalog, slot, entry);
}

static bool writable_catalog(MixtapeStore *store)
{
    if (store->result == STORE_WORKING ||
        (store->load != STORE_READY && store->load != STORE_EMPTY)) {
        return false;
    }
    if (!find_file(&store->catalog_file)) {
        HalReserveResult reserved = hal_file_reserve(store->catalog_file.path, MIX_CATALOG_BYTES);
        if ((reserved != HAL_RESERVE_CREATED && reserved != HAL_RESERVE_EXISTS) ||
            !find_file(&store->catalog_file)) {
            return false;
        }
    }
    store->load = mix_catalog_open(&store->catalog, file_io(&store->catalog_file));
    return store->load == STORE_READY || store->load == STORE_EMPTY;
}

static bool copy_name(char *to, const char *from)
{
    bool named = false;
    if (from == NULL) {
        return false;
    }
    for (int index = 0; index < MIX_NAME_BYTES; index++) {
        uint8_t value = (uint8_t)from[index];
        to[index] = (char)value;
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

static bool begin_catalog(MixtapeStore *store, MixEntry entry, uint8_t stage)
{
    MixPatch patch = {.entry = entry, .slot = store->slot};
    if (!mix_catalog_begin(&store->job.catalog, &store->catalog, store->catalog.generation,
                           patch)) {
        return false;
    }
    store->stage = stage;
    store->result = STORE_WORKING;
    return true;
}

bool mixtape_store_change(MixtapeStore *store, MixtapeChange change)
{
    if (change.operation > MIXTAPE_DELETE || change.slot >= MIX_CATALOG_SLOTS ||
        !writable_catalog(store) || !mixtape_store_read(store, change.slot, &store->entry)) {
        return false;
    }
    if (change.operation != MIXTAPE_SAVE && !store->entry.active) {
        return false;
    }
    store->slot = change.slot;
    MixEntry previous = store->entry;
    if (change.operation == MIXTAPE_DELETE) {
        store->entry = (MixEntry){.generation = previous.generation};
    } else if (!copy_name(store->entry.name, change.name)) {
        return false;
    }
    if (change.operation != MIXTAPE_SAVE) {
        return begin_catalog(store, store->entry, PUBLISH);
    }
    store->entry.active = true;
    store->entry.generation++;
    store->entry.file_bank = previous.active ? (uint8_t)(1 - previous.file_bank) : 0;
    mix_measure_begin(&store->measured, change.source);
    store->stage = MEASURE;
    store->result = STORE_WORKING;
    return true;
}

static void data_path(MixtapeStore *store)
{
    mix_tape_path(store->slot, store->entry.file_bank, store->data_file.path);
}

static bool start_check(MixtapeStore *store)
{
    data_path(store);
    if (!find_file(&store->data_file)) {
        store->tape.load = STORE_UNREADABLE;
        return false;
    }
    mix_tape_check_begin(&store->job.check, &store->tape, file_io(&store->data_file),
                         store->entry.generation);
    return true;
}

bool mixtape_store_open(MixtapeStore *store, uint16_t slot)
{
    if (store->result == STORE_WORKING || !mixtape_store_read(store, slot, &store->entry) ||
        !store->entry.active) {
        return false;
    }
    store->slot = slot;
    store->recovered = false;
    store->stage = OPEN;
    store->result = STORE_WORKING;
    if (!start_check(store)) {
        store->job.check.result = STORE_FAILED;
    }
    return true;
}

static bool allocate_tape(MixtapeStore *store)
{
    uint32_t size = STORE_BLOCK_BYTES + store->measured.bytes;
    data_path(store);
    bool exists = find_file(&store->data_file);
    if (exists && store->data_file.size < size) {
        if (!hal_file_remove(store->data_file.path)) {
            return false;
        }
        exists = false;
    }
    if (!exists) {
        HalReserveResult result = hal_file_reserve(store->data_file.path, size);
        if ((result != HAL_RESERVE_CREATED && result != HAL_RESERVE_EXISTS) ||
            !find_file(&store->data_file)) {
            return false;
        }
    }
    return mix_tape_write_begin(&store->job.writer, file_io(&store->data_file), &store->measured,
                                store->entry.generation);
}

static StoreResult measure(MixtapeStore *store)
{
    StoreResult result = mix_measure_step(&store->measured);
    if (result != STORE_DONE) {
        return result;
    }
    MixEntry previous;
    if (!mixtape_store_read(store, store->slot, &previous) ||
        !begin_catalog(store, previous, CHECKPOINT)) {
        return STORE_FAILED;
    }
    store->entry.count = store->measured.source.count;
    store->entry.bytes = store->measured.bytes;
    return STORE_WORKING;
}

static bool previous_tape(MixtapeStore *store)
{
    MixCatalog previous = store->catalog;
    MixEntry entry;
    if (store->recovered || mix_catalog_previous(&previous) != STORE_READY ||
        !mix_catalog_read(&previous, store->slot, &entry) || !entry.active ||
        (entry.generation == store->entry.generation &&
         entry.file_bank == store->entry.file_bank)) {
        return false;
    }
    store->entry = entry;
    store->recovered = true;
    return start_check(store);
}

static StoreResult open_tape(MixtapeStore *store)
{
    StoreResult result = mix_tape_check_step(&store->job.check);
    if (result == STORE_DONE &&
        (store->entry.count != store->tape.count || store->entry.bytes != store->tape.bytes)) {
        result = STORE_FAILED;
        store->tape.load = STORE_CORRUPT;
    }
    if (result == STORE_DONE && store->recovered) {
        return begin_catalog(store, store->entry, RECOVER) ? STORE_WORKING : STORE_FAILED;
    }
    if (result == STORE_FAILED && store->tape.load != STORE_UNSUPPORTED && previous_tape(store)) {
        return STORE_WORKING;
    }
    return result;
}

StoreResult mixtape_store_step(MixtapeStore *store)
{
    if (store->result != STORE_WORKING) {
        return store->result;
    }
    StoreResult result = STORE_FAILED;
    switch (store->stage) {
    case MEASURE:
        result = measure(store);
        break;
    case CHECKPOINT:
        result = mix_catalog_step(&store->job.catalog);
        if (result == STORE_DONE) {
            store->load = STORE_READY;
            store->stage = ALLOCATE;
            result = STORE_WORKING;
        }
        break;
    case ALLOCATE:
        result = allocate_tape(store) ? STORE_WORKING : STORE_FAILED;
        store->stage = WRITE_TAPE;
        break;
    case WRITE_TAPE:
        result = mix_tape_write_step(&store->job.writer);
        if (result == STORE_DONE) {
            result = begin_catalog(store, store->entry, PUBLISH) ? STORE_WORKING : STORE_FAILED;
        }
        break;
    case PUBLISH:
    case RECOVER:
        result = mix_catalog_step(&store->job.catalog);
        if (result == STORE_DONE) {
            store->load = STORE_READY;
        }
        break;
    case OPEN:
        result = open_tape(store);
        break;
    }
    store->result = result;
    return result;
}

void mixtape_store_cancel(MixtapeStore *store)
{
    if (store->result == STORE_WORKING) {
        if (store->stage == CHECKPOINT || store->stage == PUBLISH || store->stage == RECOVER) {
            mix_catalog_abort(&store->job.catalog);
        } else if (store->stage == WRITE_TAPE) {
            mix_tape_write_abort(&store->job.writer);
        }
        store->result = STORE_FAILED;
    }
}
