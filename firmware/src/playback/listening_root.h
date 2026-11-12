#pragma once

#include "playback/listening_log.h"

enum {
    ROOT_NEXT = 0,
    ROOT_KEPT = 4,
    ROOT_LISTENED = 8,
    ROOT_PLAYS = 16,
    ROOT_SKIPS = 20,
    ROOT_OUTPUTS = 24,
    ROOT_FORMATS = ROOT_OUTPUTS + 8 * LISTENING_OUTPUT_COUNT,
    ROOT_BOOTS = ROOT_FORMATS + 8 * LISTENING_FORMAT_COUNT,
    ROOT_CHARGES = ROOT_BOOTS + 4,
    ROOT_FULL = ROOT_CHARGES + 4,
    ROOT_BYTES = ROOT_FULL + 4
};

static const StoreKind KIND = {.magic = {'D', 'S', 'L', 'L'}, .version = 1, .inline_payload = true};

typedef struct Reader {
    const StoreFile *file;
    ListeningRoot root;
} Reader;

static uint64_t get64(const uint8_t *bytes)
{
    return (uint64_t)store_get32(bytes) | (uint64_t)store_get32(bytes + 4) << 32;
}

static void decode_times(const uint8_t *bytes, ListeningCounters *counters)
{
    for (int index = 0; index < LISTENING_OUTPUT_COUNT; index++) {
        counters->output_ms[index] = get64(bytes + ROOT_OUTPUTS + 8 * index);
    }
    for (int index = 0; index < LISTENING_FORMAT_COUNT; index++) {
        counters->format_ms[index] = get64(bytes + ROOT_FORMATS + 8 * index);
    }
}

static bool decode_root(const uint8_t *bytes, ListeningRoot *root)
{
    ListeningCounters *counters = &root->counters;
    uint32_t kept = store_get32(bytes + ROOT_KEPT);
    root->next = store_get32(bytes + ROOT_NEXT);
    root->kept = (uint16_t)kept;
    counters->totals.listened_ms = get64(bytes + ROOT_LISTENED);
    counters->totals.plays = store_get32(bytes + ROOT_PLAYS);
    counters->totals.skips = store_get32(bytes + ROOT_SKIPS);
    decode_times(bytes, counters);
    counters->boots = store_get32(bytes + ROOT_BOOTS);
    counters->charge_sessions = store_get32(bytes + ROOT_CHARGES);
    counters->full_charges = store_get32(bytes + ROOT_FULL);
    return kept <= LISTENING_LOG_CAPACITY;
}

static StoreSlot slot_of(const StoreFile *file, uint8_t bank)
{
    return (StoreSlot){.file = file, .kind = &KIND, .offset = (uint32_t)bank * STORE_BLOCK_BYTES};
}

static bool file_valid(const StoreFile *file)
{
    return file->source.read != NULL && file->size >= LISTENING_LOG_FILE_BYTES;
}

static StoreLoad check_root(void *context, uint8_t bank, const StoreHeader *header)
{
    Reader *reader = context;
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader again;
    StoreSlot slot = slot_of(reader->file, bank);
    StoreLoad load = store_load(&slot, block, &again);
    if (load != STORE_READY) {
        return load;
    }
    bool decoded = again.generation == header->generation && again.bytes == ROOT_BYTES &&
                   decode_root(block + STORE_FIELD_BYTES, &reader->root);
    return decoded ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad load_root(ListeningLog *log)
{
    Reader reader = {.file = &log->file};
    StorePair pair = {.file = &log->file,
                      .kind = &KIND,
                      .bank_bytes = STORE_BLOCK_BYTES,
                      .check = check_root,
                      .context = &reader};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    if (load == STORE_READY) {
        log->root = reader.root;
        log->generation = choice.header.generation;
        log->bank = choice.bank;
        log->valid = true;
    }
    return load;
}
