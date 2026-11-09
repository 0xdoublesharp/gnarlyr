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

static MixIO file_io(MixtapeFile *file)
{
    return (MixIO){.source = {read_file, file}, .write = write_file, .size = file->size};
}

MixLoad mixtape_store_init(MixtapeStore *store)
{
    *store = (MixtapeStore){.catalog_file.path = "/.echo/mixes2.bin"};
    store->load = find_file(&store->catalog_file)
                      ? mix_catalog_open(&store->catalog, file_io(&store->catalog_file))
                      : MIX_EMPTY;
    return store->load;
}

bool mixtape_store_read(const MixtapeStore *store, uint16_t slot, MixEntry *entry)
{
    if (slot >= MIX_CATALOG_SLOTS || (store->load != MIX_EMPTY && store->load != MIX_READY)) {
        return false;
    }
    if (store->load == MIX_EMPTY) {
        *entry = (MixEntry){0};
        return true;
    }
    return mix_catalog_read(&store->catalog, slot, entry);
}

static bool writable_catalog(MixtapeStore *store)
{
    if (store->result == MIX_WORKING || (store->load != MIX_READY && store->load != MIX_EMPTY)) {
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
    return store->load == MIX_READY || store->load == MIX_EMPTY;
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
    store->result = MIX_WORKING;
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
    store->result = MIX_WORKING;
    return true;
}

static void data_path(MixtapeStore *store)
{
    StringBuilder builder =
        string_builder_make(store->data_file.path, sizeof store->data_file.path);
    string_builder_append(&builder, "/.echo/MX");
    string_builder_append_char(&builder, (char)('0' + store->slot / 10));
    string_builder_append_char(&builder, (char)('0' + store->slot % 10));
    string_builder_append(&builder, store->entry.file_bank == 0 ? "A.BIN" : "B.BIN");
}

static bool start_check(MixtapeStore *store)
{
    data_path(store);
    if (!find_file(&store->data_file)) {
        store->tape.load = MIX_UNREADABLE;
        return false;
    }
    mix_tape_check_begin(&store->job.check, &store->tape, file_io(&store->data_file),
                         store->entry.generation);
    return true;
}

bool mixtape_store_open(MixtapeStore *store, uint16_t slot)
{
    if (store->result == MIX_WORKING || !mixtape_store_read(store, slot, &store->entry) ||
        !store->entry.active) {
        return false;
    }
    store->slot = slot;
    store->recovered = false;
    store->stage = OPEN;
    store->result = MIX_WORKING;
    if (!start_check(store)) {
        store->job.check.result = MIX_FAILED;
    }
    return true;
}

static bool allocate_tape(MixtapeStore *store)
{
    uint32_t size = MIX_HEADER_BYTES + store->measured.bytes;
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

static MixResult measure(MixtapeStore *store)
{
    MixResult result = mix_measure_step(&store->measured);
    if (result != MIX_DONE) {
        return result;
    }
    MixEntry previous;
    if (!mixtape_store_read(store, store->slot, &previous) ||
        !begin_catalog(store, previous, CHECKPOINT)) {
        return MIX_FAILED;
    }
    store->entry.count = store->measured.source.count;
    store->entry.bytes = store->measured.bytes;
    return MIX_WORKING;
}

static bool previous_tape(MixtapeStore *store)
{
    MixCatalog previous = store->catalog;
    MixEntry entry;
    if (store->recovered || mix_catalog_previous(&previous) != MIX_READY ||
        !mix_catalog_read(&previous, store->slot, &entry) || !entry.active ||
        (entry.generation == store->entry.generation &&
         entry.file_bank == store->entry.file_bank)) {
        return false;
    }
    store->entry = entry;
    store->recovered = true;
    return start_check(store);
}

static MixResult open_tape(MixtapeStore *store)
{
    MixResult result = mix_tape_check_step(&store->job.check);
    if (result == MIX_DONE &&
        (store->entry.count != store->tape.count || store->entry.bytes != store->tape.bytes)) {
        result = MIX_FAILED;
        store->tape.load = MIX_CORRUPT;
    }
    if (result == MIX_DONE && store->recovered) {
        return begin_catalog(store, store->entry, RECOVER) ? MIX_WORKING : MIX_FAILED;
    }
    if (result == MIX_FAILED && store->tape.load != MIX_UNSUPPORTED && previous_tape(store)) {
        return MIX_WORKING;
    }
    return result;
}

MixResult mixtape_store_step(MixtapeStore *store)
{
    if (store->result != MIX_WORKING) {
        return store->result;
    }
    MixResult result = MIX_FAILED;
    switch (store->stage) {
    case MEASURE:
        result = measure(store);
        break;
    case CHECKPOINT:
        result = mix_catalog_step(&store->job.catalog);
        if (result == MIX_DONE) {
            store->load = MIX_READY;
            store->stage = ALLOCATE;
            result = MIX_WORKING;
        }
        break;
    case ALLOCATE:
        result = allocate_tape(store) ? MIX_WORKING : MIX_FAILED;
        store->stage = WRITE_TAPE;
        break;
    case WRITE_TAPE:
        result = mix_tape_write_step(&store->job.writer);
        if (result == MIX_DONE) {
            result = begin_catalog(store, store->entry, PUBLISH) ? MIX_WORKING : MIX_FAILED;
        }
        break;
    case PUBLISH:
    case RECOVER:
        result = mix_catalog_step(&store->job.catalog);
        if (result == MIX_DONE) {
            store->load = MIX_READY;
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
    if (store->result == MIX_WORKING) {
        if (store->stage == CHECKPOINT || store->stage == PUBLISH || store->stage == RECOVER) {
            mix_catalog_abort(&store->job.catalog);
        } else if (store->stage == WRITE_TAPE) {
            mix_tape_write_abort(&store->job.writer);
        }
        store->result = MIX_FAILED;
    }
}
